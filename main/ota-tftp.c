/* TFTP Server OTA support
 *
 * For details of use see ota-tftp.h
 *
 * Part of esp-open-rtos
 * Copyright (C) 2015 Superhouse Automation Pty Ltd
 * BSD Licensed as described in the file LICENSE
 */
#include <freertos/FreeRTOS.h>
#include <string.h>
#include <strings.h>

#include "lwip/err.h"
#include "lwip/api.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "lwip/mem.h"
#include "lwip/netbuf.h"

#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_flash_partitions.h"
#include "esp_image_format.h"

#include "ota-tftp.h"

/* Read a 16 bit wide unsigned integer, stored host order, from the netbuf */
inline static u16_t netbuf_read_u16_h(struct netbuf *netbuf, u16_t offs)
{
	u16_t raw;
	netbuf_copy_partial(netbuf, &raw, 2, offs);
	return raw;
}

inline static u16_t netbuf_read_u16_n(struct netbuf *netbuf, u16_t offs)
{
	return ntohs(netbuf_read_u16_h(netbuf, offs));
}

/* Read an 8 bit unsigned integer from the netbuf */
inline static u8_t netbuf_read_u8(struct netbuf *netbuf, u16_t offs)
{
	u8_t result;
	netbuf_copy_partial(netbuf, &result, 1, offs);
	return result;
}

#define TFTP_FIRMWARE_FILE "firmware.bin"
#define TFTP_OCTET_MODE "octet" /* non-case-sensitive */

#define TFTP_OP_RRQ 1
#define TFTP_OP_WRQ 2
#define TFTP_OP_DATA 3
#define TFTP_OP_ACK 4
#define TFTP_OP_ERROR 5
#define TFTP_OP_OACK 6

#define TFTP_ERR_FILENOTFOUND 1
#define TFTP_ERR_FULL 3
#define TFTP_ERR_ILLEGAL 4
#define TFTP_ERR_BADID 5

#define MAX_IMAGE_SIZE 0x100000 /*1MB images max at the moment */

static void tftp_task(void *port_p);
static char *tftp_get_field(int field, struct netbuf *netbuf);
static err_t tftp_receive_data(struct netconn *nc, size_t *received_len, ip_addr_t *peer_addr, int peer_port,
			       tftp_receive_cb receive_cb);
static err_t tftp_send_ack(struct netconn *nc, int block);
static err_t tftp_send_rrq(struct netconn *nc, const char *filename);
static void tftp_send_error(struct netconn *nc, int err_code, const char *err_msg);

esp_ota_handle_t update_handle;
const esp_partition_t *update_part;
const esp_partition_t *configured_part;
const esp_partition_t *running_part;

#define TAG "ota-tftp"

void ota_tftp_init_server(int listen_port, int prio)
{
	xTaskCreate(tftp_task, "tftpOTATask", 2500, (void *)listen_port, prio, NULL);
}

static int ota_tftp_init()
{
	configured_part = esp_ota_get_boot_partition();
	running_part = esp_ota_get_running_partition();
	if (!configured_part || !running_part) {
		ESP_LOGE(TAG, "configured or running parititon is null, is OTA support enabled in build "
			      "configuration?");
		return ERR_VAL;
	}

	if (configured_part != running_part) {
		ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
			 configured_part->address, running_part->address);
		ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted "
			      "somehow.)");
	}
	ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)", running_part->type, running_part->subtype,
		 running_part->address);

	update_part = esp_ota_get_next_update_partition(NULL);

	if (update_part == NULL) {
		ESP_LOGE(TAG, "update_partition not found!");
		return ERR_VAL;
	}
	ESP_LOGI(TAG, "esp_ota_begin");
	err_t err = esp_ota_begin(update_part, OTA_SIZE_UNKNOWN, &update_handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_begin failed, error=%d", err);
		return ERR_VAL;
	} else {
		ESP_LOGI(TAG, "esp_ota_begin succeeded");
	}
	return ERR_OK;
}

err_t ota_tftp_download(const char *server, int port, const char *filename, int timeout, int ota_slot,
			tftp_receive_cb receive_cb)
{
	err_t err;

	if ((err = ota_tftp_init()) < 0) {
		return ERR_VAL;
	}

	struct netconn *nc = netconn_new(NETCONN_UDP);
	if (!nc) {
		return ERR_IF;
	}

	netconn_set_recvtimeout(nc, timeout);

	/* try to bind our client port as our local port,
       or keep trying the next 10 ports after it */
	int local_port = port - 1;
	do {
		err = netconn_bind(nc, IP_ADDR_ANY, ++local_port);
	} while (err == ERR_USE && local_port < port + 10);
	if (err) {
		netconn_delete(nc);
		return err;
	}

	ip_addr_t addr;
	err = netconn_gethostbyname(server, &addr);
	if (err) {
		netconn_delete(nc);
		return err;
	}

	netconn_connect(nc, &addr, port);

	err = tftp_send_rrq(nc, filename);
	if (err) {
		netconn_delete(nc);
		return err;
	}

	size_t received_len;
	err = tftp_receive_data(nc, &received_len, &addr, port, receive_cb);
	netconn_delete(nc);
	return err;
}

static void tftp_task(void *listen_port)
{
	struct netconn *nc = netconn_new(NETCONN_UDP);
	//int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (!nc) {
		ESP_LOGE(__func__, "OTA TFTP: Failed to allocate socket.");
		return;
	}

	// struct sockaddr_in addr;
	// addr.sin_addr.s_addr = 0;
	// addr.sin_family = AF_INET;
	// addr.sin_port = ntohs((int)listen_port);
	netconn_bind(nc, IP_ADDR_ANY, (int)listen_port);
	//bind(sock, (struct socaddr*)&addr, sizeof(addr));

	/* We expect a WRQ packet with filename "firmware.bin" and "octet" mode,
    */
	while (1) {
		/* wait as long as needed for a WRQ packet */
		netconn_set_recvtimeout(nc, 0);
		struct netbuf *netbuf;
		err_t err = netconn_recv(nc, &netbuf);
		if (err != ERR_OK) {
			ESP_LOGE(__func__, "OTA TFTP Error: Failed to receive TFTP initial packet. err=%d", err);
			continue;
		}
		uint16_t len = netbuf_len(netbuf);
		if (len < 6) {
			ESP_LOGE(__func__, "OTA TFTP Error: Packet too short for a valid WRQ");
			netbuf_delete(netbuf);
			continue;
		}

		uint16_t opcode = netbuf_read_u16_n(netbuf, 0);
		if (opcode != TFTP_OP_WRQ) {
			ESP_LOGE(__func__, "OTA TFTP Error: Invalid opcode 0x%04x didn't match WRQ", opcode);
			netbuf_delete(netbuf);
			continue;
		}

		/* check filename */
		char *filename = tftp_get_field(0, netbuf);
		if (!filename || strcmp(filename, TFTP_FIRMWARE_FILE)) {
			tftp_send_error(nc, TFTP_ERR_FILENOTFOUND, "File must be firmware.bin");
			free(filename);
			netbuf_delete(netbuf);
			continue;
		}
		free(filename);

		/* check mode */
		char *mode = tftp_get_field(1, netbuf);
		if (!mode || strcmp(TFTP_OCTET_MODE, mode)) {
			tftp_send_error(nc, TFTP_ERR_ILLEGAL, "Mode must be octet/binary");
			free(mode);
			netbuf_delete(netbuf);
			continue;
		}
		free(mode);

		/* establish a connection back to the sender from this netbuf */
		netconn_connect(nc, netbuf_fromaddr(netbuf), netbuf_fromport(netbuf));
		netbuf_delete(netbuf);

		/* ACK the WRQ */
		int ack_err = tftp_send_ack(nc, 0);
		if (ack_err != 0) {
			ESP_LOGE(__func__, "OTA TFTP initial ACK failed");
			netconn_disconnect(nc);
			continue;
		}

		/* init ota system */

		ota_tftp_init();

		/* Finished WRQ phase, start TFTP data transfer */
		size_t received_len;
		netconn_set_recvtimeout(nc, 10000);
		int recv_err = tftp_receive_data(nc, &received_len, NULL, 0, NULL);

		netconn_disconnect(nc);
		ESP_LOGI(TAG, "OTA TFTP receive data result %d, bytes %d", recv_err, received_len);
		if (recv_err == ERR_OK) {
			err = esp_ota_set_boot_partition(update_part);
			if (err != ESP_OK) {
				ESP_LOGE(TAG, "esp_ota_set_boot_partition failed! err=0x%x", err);
				continue;
			}

			esp_restart();
		}
	}
}

/* Return numbered field in a TFTP RRQ/WRQ packet

   Uses dest_buf (length dest_len) for temporary storage, so dest_len must be
   at least as long as the longest valid/expected field.

   Result is either NULL if an error occurs, or a newly malloced string that the
   caller needs to free().
 */
static char *tftp_get_field(int field, struct netbuf *netbuf)
{
	int offs = 2;
	int field_offs = 2;
	int field_len = 0;
	/* Starting past the opcode, skip all previous fields then find start/len of ours */
	while (field >= 0 && offs < netbuf_len(netbuf)) {
		char c = netbuf_read_u8(netbuf, offs++);
		if (field == 0) {
			field_len++;
		}
		if (c == 0) {
			field--;
			if (field == 0)
				field_offs = offs;
		}
	}

	if (field != -1)
		return NULL;

	char *result = malloc(field_len);
	netbuf_copy_partial(netbuf, result, field_len, field_offs);
	return result;
}

#define TFTP_TIMEOUT_RETRANSMITS 10

static err_t tftp_receive_data(struct netconn *nc, size_t *received_len, ip_addr_t *peer_addr, int peer_port,
			       tftp_receive_cb receive_cb)
{
	*received_len = 0;
	const int DATA_PACKET_SZ = 512 + 4; /*( packet size plus header */
	int block = 1;

	struct netbuf *netbuf = 0;
	int retries = TFTP_TIMEOUT_RETRANSMITS;

	while (1) {
		if (peer_addr) {
			netconn_disconnect(nc);
		}

		err_t err = netconn_recv(nc, &netbuf);

		if (peer_addr) {
			if (netbuf) {
				/* For TFTP server, the UDP connection is already established. But for client,
                   we don't know what port the server is using until we see the first data
                   packet - so we connect here.
                */
				netconn_connect(nc, netbuf_fromaddr(netbuf), netbuf_fromport(netbuf));
				peer_addr = 0;
			} else {
				/* Otherwise, temporarily re-connect so we can send errors */
				netconn_connect(nc, peer_addr, peer_port);
			}
		}

		if (err == ERR_TIMEOUT) {
			if (retries-- > 0 && block > 1) {
				/* Retransmit the last ACK, wait for repeat data block.

                 This doesn't work for the first block, have to time out and start again. */
				tftp_send_ack(nc, block - 1);
				continue;
			}
			tftp_send_error(nc, TFTP_ERR_ILLEGAL, "Timeout");
			return ERR_TIMEOUT;
		} else if (err != ERR_OK) {
			tftp_send_error(nc, TFTP_ERR_ILLEGAL, "Failed to receive packet");
			return err;
		}

		uint16_t opcode = netbuf_read_u16_n(netbuf, 0);
		if (opcode != TFTP_OP_DATA) {
			tftp_send_error(nc, TFTP_ERR_ILLEGAL, "Unknown opcode");
			netbuf_delete(netbuf);
			return ERR_VAL;
		}

		uint16_t client_block = netbuf_read_u16_n(netbuf, 2);
		if (client_block != block) {
			netbuf_delete(netbuf);
			if (client_block == block - 1) {
				/* duplicate block, means our ack got lost */
				tftp_send_ack(nc, block - 1);
				continue;
			} else {
				tftp_send_error(nc, TFTP_ERR_ILLEGAL, "Block# out of order");
				return ERR_VAL;
			}
		}

		/* Reset retry count if we got valid data */
		retries = TFTP_TIMEOUT_RETRANSMITS;

		/* One UDP packet can be more than one netbuf segment, so iterate all the
           segments in the netbuf and write them to flash
        */
		int len = netbuf_len(netbuf);

		bool first_chunk = true;
		do {
			uint16_t chunk_len;
			uint32_t *chunk;
			netbuf_data(netbuf, (void **)&chunk, &chunk_len);
			if (first_chunk) {
				chunk++;	/* skip the 4 byte TFTP header */
				chunk_len -= 4; /* assuming this netbuf chunk is at least 4 bytes! */
				first_chunk = false;
			}

			err = esp_ota_write(update_handle, chunk, chunk_len);
			if (err != ESP_OK) {
				ESP_LOGE(TAG, "Error: esp_ota_write failed! err=0x%x", err);
				return ERR_VAL;
			}

		} while (netbuf_next(netbuf) >= 0);

		netbuf_delete(netbuf);

		*received_len += len - 4;

		//        if(len < DATA_PACKET_SZ) {
		//            /* This was the last block, but verify the image before we ACK
		//               it so the client gets an indication if things were successful.
		//            */
		//            const char *err = "Unknown validation error";
		//            uint32_t image_length;
		//            if(!rboot_verify_image(start_offs, &image_length, &err)
		//               || image_length != *received_len) {
		//                tftp_send_error(nc, TFTP_ERR_ILLEGAL, err);
		//                return ERR_VAL;
		//            }
		//        }

		err_t ack_err = tftp_send_ack(nc, block);
		if (ack_err != ERR_OK) {
			ESP_LOGE(__func__, "OTA TFTP failed to send ACK.");
			return ack_err;
		}

		// Make sure ack was successful before calling callback.
		if (receive_cb) {
			receive_cb(*received_len);
		}

		if (len < DATA_PACKET_SZ) {
			return ERR_OK;
		}

		block++;
	}
}

static err_t tftp_send_ack(struct netconn *nc, int block)
{
	/* Send ACK */
	struct netbuf *resp = netbuf_new();
	uint16_t *ack_buf = (uint16_t *)netbuf_alloc(resp, 4);
	ack_buf[0] = htons(TFTP_OP_ACK);
	ack_buf[1] = htons(block);
	err_t ack_err = netconn_send(nc, resp);
	netbuf_delete(resp);
	return ack_err;
}

static void tftp_send_error(struct netconn *nc, int err_code, const char *err_msg)
{
	ESP_LOGE(__func__, "OTA TFTP Error: %s", err_msg);
	struct netbuf *err = netbuf_new();
	uint16_t *err_buf = (uint16_t *)netbuf_alloc(err, 4 + strlen(err_msg) + 1);
	err_buf[0] = htons(TFTP_OP_ERROR);
	err_buf[1] = htons(err_code);
	strcpy((char *)&err_buf[2], err_msg);
	netconn_send(nc, err);
	netbuf_delete(err);
}

static err_t tftp_send_rrq(struct netconn *nc, const char *filename)
{
	struct netbuf *rrqbuf = netbuf_new();
	uint16_t *rrqdata = (uint16_t *)netbuf_alloc(rrqbuf, 4 + strlen(filename) + strlen(TFTP_OCTET_MODE));
	rrqdata[0] = htons(TFTP_OP_RRQ);
	char *rrq_filename = (char *)&rrqdata[1];
	strcpy(rrq_filename, filename);
	strcpy(rrq_filename + strlen(filename) + 1, TFTP_OCTET_MODE);

	err_t err = netconn_send(nc, rrqbuf);
	netbuf_delete(rrqbuf);
	return err;
}

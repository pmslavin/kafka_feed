#define USE_LZ4
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "kafkaops.h"


static const char *broker_list = "10.0.38.243,10.0.38.244";
static const char *topic       = "paul_test";
static char txt[] = "Text from initialise Kafka";

static char errbuf[1024];

static rd_kafka_t		*rk  = NULL;
static rd_kafka_topic_t	*rkt = NULL;


static void dr_msg_cb(rd_kafka_t *rk, const rd_kafka_message_t *rkmsg, void *arg)
{
	(void)rk;	// unused
	(void)arg;	// unused

	if(rkmsg->err)
		rd_kafka_err2str(rkmsg->err);
	else
		fprintf(stderr, "Published %zd bytes to partition %u\n", rkmsg->len, rkmsg->partition);
}


int init_kafka_producer(void)
{
	rd_kafka_conf_t *conf = rd_kafka_conf_new();

	if(rd_kafka_conf_set(conf, "bootstrap.servers", broker_list, errbuf, sizeof(errbuf)) != RD_KAFKA_CONF_OK){
		fprintf(stderr, "%s\n", errbuf);
		return -1;
	}
#ifdef USE_LZ4
	if(rd_kafka_conf_set(conf, "compression.codec", "lz4", errbuf, sizeof(errbuf)) != RD_KAFKA_CONF_OK)
		fprintf(stderr, "Unable to select LZ4 compression\n");
#endif
	if(rd_kafka_conf_set(conf, "message.max.bytes", "20971520", errbuf, sizeof(errbuf)) != RD_KAFKA_CONF_OK)
		fprintf(stderr, "Unable to set max.message.bytes\n");

	rd_kafka_conf_set_dr_msg_cb(conf, dr_msg_cb);

	rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errbuf, sizeof(errbuf));

	if(!rk){
		fprintf(stderr, "producer: %s\n", errbuf);
		return -1;
	}

	rkt = rd_kafka_topic_new(rk, topic, NULL);

	int ret = rd_kafka_produce(rkt, RD_KAFKA_PARTITION_UA, 0, txt, sizeof(txt), NULL, 0, NULL);
	if(ret == -1){
		fprintf(stderr, "%% Failed to produce to topic %s: %s\n", rd_kafka_topic_name(rkt), rd_kafka_err2str(rd_kafka_last_error()));
		return -1;
	}

	rd_kafka_poll(rk, 0);
	rd_kafka_flush(rk, 10*1000);

	return 0;
}

int close_kafka_producer(void)
{
	if(!(rk || rkt))
		return -1;

	/* Final flush of message queue */
	rd_kafka_flush(rk, 5*1000);

	rd_kafka_topic_destroy(rkt);
	rd_kafka_destroy(rk);

	return 0;
}


char *form_msg(const fileinfo_t *f, size_t *msg_size)
{
	const char *prefix = "{file : '%s', size : %lu, mtime : %lu, sha256 : '%s', data : '";
	const char *suffix = "'}";
	int pflen  = strlen(prefix) + strlen(f->path) + 20 /* floor(log10(2^64-1))+1 */ + 2*SHA256_DIGEST_LENGTH;
	int buflen = pflen + f->size + strlen(suffix);

	char *buf = malloc(buflen);
	if(!buf){
		perror("form_msg: malloc");
		*msg_size = -1;
		return NULL;
	}
	memset(buf, 0, buflen);
	*msg_size = buflen;

	int pfoff = snprintf(buf, pflen+1, prefix, f->path, f->size, f->mtime, f->digest);

	int fd = open(f->path, 'r');
	if(fd == -1){
		perror("form_msg: open");
		goto error;
	}

	void *mf = mmap(NULL, f->size, PROT_READ, MAP_SHARED, fd, 0);
	if(!mf){
		perror("form_msg: mmap");
		goto error;
	}
	close(fd);

	memcpy(buf+pfoff, mf, f->size);
	munmap(mf, f->size);

	int sfoff = snprintf(buf+pfoff+f->size, strlen(suffix)+1, "%s", suffix);

	/* Trim buf to exact msg size */
	void *rbuf = realloc(buf, pfoff+f->size+sfoff);
	if(!rbuf){
		perror("form_msg: realloc");
		goto error;
	}
//	fprintf(stderr, "buf: %x  rbuf: %x\n", buf, rbuf);

	*msg_size = pfoff+f->size+sfoff;
	return buf;

error:
	*msg_size = -1;
	free(buf);
	return NULL;
}


int publish(const char *msg, size_t msg_size)
{
	int ret = rd_kafka_produce(rkt, RD_KAFKA_PARTITION_UA, 0, (void*)msg, msg_size, NULL, 0, NULL);
	if(ret == -1){
		fprintf(stderr, "%% Failed to produce to topic %s: %s\n", rd_kafka_topic_name(rkt), rd_kafka_err2str(rd_kafka_last_error()));
		return -1;
	}

	rd_kafka_poll(rk, 0);
	rd_kafka_flush(rk, 10*1000);

	return 0;
}

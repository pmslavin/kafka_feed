#define USE_LZ4
#define _POSIX_C_SOURCE 200809L	// getline
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>

#include "kafkaops.h"
#include "utils.h"


const char *complete_dir = "/tmp/processed";
static const char *topic  = "paul_test";
static char connect_txt[] = "[%s] Connection from CDR Monitor producer (%u)";	// [isotime] ... (pid)

static char errbuf[1024];

static rd_kafka_t		*rk  = NULL;
static rd_kafka_topic_t	*rkt = NULL;


static void dr_msg_cb(rd_kafka_t *rk, const rd_kafka_message_t *rkmsg, void *arg)
{
	(void)rk;	// unused
	(void)arg;	// unused

	if(rkmsg->err)
		rd_kafka_err2str(rkmsg->err);
//	else
//		fprintf(stderr, "Published %zd bytes to partition %u\n", rkmsg->len, rkmsg->partition);
}


int init_kafka_producer(void)
{
	struct kafka_config_pair kcp_conf[] = { {"bootstrap.servers", "10.0.38.243,10.0.38.244", 1},
#ifdef USE_LZ4
											{"compression.codec", "lz4", 0},
#endif
											{"message.max.bytes", "20971520", 0},
											{"batch.num.messages", "2000", 0},
											{"queue.buffering.max.kbytes", "131072", 0},
											{"queue.buffering.max.ms", "100", 0},
											{"request.required.acks", "all", 0}
										  };

	rd_kafka_conf_t *conf = rd_kafka_conf_new();

	for(size_t c=0; c < sizeof(kcp_conf)/sizeof(struct kafka_config_pair); c++){
		if(rd_kafka_conf_set(conf, kcp_conf[c].key, kcp_conf[c].val, errbuf, sizeof(errbuf)) != RD_KAFKA_CONF_OK){
			fprintf(stderr, "Unable to set %s\n", kcp_conf[c].key);
			if(kcp_conf[c].fail_on_err)
				return -1;
		}
	}

	rd_kafka_conf_set_dr_msg_cb(conf, dr_msg_cb);

	rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errbuf, sizeof(errbuf));
	if(!rk){
		fprintf(stderr, "producer: %s\n", errbuf);
		return -1;
	}

	rkt = rd_kafka_topic_new(rk, topic, NULL);

	char connect_line[72];
	char connect_time[ISO_TIME_SZ];
	isotime(connect_time);
	if(!monitor_pid)
		monitor_pid = getpid();
	int line_size = snprintf(connect_line, 72, connect_txt, connect_time, monitor_pid);

	int ret = rd_kafka_produce(rkt, RD_KAFKA_PARTITION_UA, 0, connect_line, line_size, NULL, 0, NULL);
	if(ret == -1){
		fprintf(stderr, "%% Failed to produce to topic %s: %s\n", rd_kafka_topic_name(rkt), rd_kafka_err2str(rd_kafka_last_error()));
		return -1;
	}

	const struct rd_kafka_metadata *metadata;

	rd_kafka_resp_err_t err = rd_kafka_metadata(rk, 1, rkt, &metadata, 5000);
	if(err != RD_KAFKA_RESP_ERR_NO_ERROR){
		fprintf(stderr, "Unable to retrieve Kafka metadata\n");
	}else{
		char log_time[ISO_TIME_SZ];
		isotime(log_time);
		fprintf(stderr, "[%s] Connected to brokers:\n", log_time);
		for(int i=metadata->broker_cnt-1; i>=0; i--){	// ids descend?
			fprintf(stderr, "  id: %u  %s:%i\n", metadata->brokers[i].id,
												 metadata->brokers[i].host,
												 metadata->brokers[i].port);
		}
		rd_kafka_metadata_destroy(metadata);
	}
	rd_kafka_poll(rk, 0);
	rd_kafka_flush(rk, 5*1000);

	return 0;
}


int close_kafka_producer(void)
{
	if(!(rk || rkt))
		return -1;

	/* Final flush of message queue */
	rd_kafka_flush(rk, 1000);
	rd_kafka_poll(rk, 1000);

	rd_kafka_topic_destroy(rkt);
	rd_kafka_destroy(rk);

	return 0;
}


char *form_file_msg(const fileinfo_t *f, size_t *msg_size)
{
	const char *prefix = "{file : '%s', size : %lu, mtime : %lu, sha256 : '%s', data : '";
	const char *suffix = "'}";
	int pflen  = strlen(prefix) + strlen(f->path) + 2*20 /* 2*floor(log10(2^64-1))+1 */ + 2*SHA256_DIGEST_LENGTH;
	int buflen = pflen + f->size + strlen(suffix);

	char *buf = malloc(buflen);
	if(!buf){
		perror("form_file_msg: malloc");
		*msg_size = -1;
		return NULL;
	}
	memset(buf, 0, buflen);
	*msg_size = buflen;

	int pfoff = snprintf(buf, pflen+1, prefix, f->path, f->size, f->mtime, f->digest);

	int fd = open(f->path, O_RDONLY);
	if(fd == -1){
		perror("form_file_msg: open");
		goto error;
	}

	void *mf = mmap(NULL, f->size, PROT_READ, MAP_SHARED, fd, 0);
	if(!mf){
		perror("form_file_msg: mmap");
		goto error;
	}
	close(fd);

	memcpy(buf+pfoff, mf, f->size);
	munmap(mf, f->size);

	int sfoff = snprintf(buf+pfoff+f->size, strlen(suffix)+1, "%s", suffix);

	/* Trim buf to exact msg size: NB not null-terminated */
	void *rbuf = realloc(buf, pfoff+f->size+sfoff);
	if(!rbuf){
		perror("form_file_msg: realloc");
		goto error;
	}

	*msg_size = pfoff+f->size+sfoff;
	return buf;

error:
	*msg_size = -1;
	free(buf);
	return NULL;
}


size_t enqueue_cdr_msgs(cdrmsg_t **q, const fileinfo_t *f)
{
	size_t msg_count = 0, line_len = 0;
	ssize_t count = 0;
	char *line = NULL, *p = NULL;

	FILE *fp = fopen(f->path, "r");
	if(!fp){
		perror("enqueue_cdr_msgs: fopen");
		return -1;
	}

	cdrmsg_t *qhead = *q;
	cdrmsg_t *qlast = *q;
	while(qhead){
		qlast = qhead;
		qhead = qhead->next;
	}

	while((count = getline(&line, &line_len, fp)) != -1){
		p = line;
		p[count-1] = '\0';	// Replace final newline with null
		msg_count++;

		char *msg = form_cdr_msg(f, line);
		free(line);
		cdrmsg_t *cm = malloc(sizeof(cdrmsg_t));
		if(!cm){
			perror("enqueue_cdr_msgs: malloc");
			free(msg);
			return -1;
		}
		cm->next = NULL;
		cm->msg  = msg;
		if(qlast)
			qlast->next = cm;
		else
			*q = cm;

		qlast = cm;
		line = NULL;
		line_len = 0;
	}

	if(count == -1)
		free(line);

	fclose(fp);
	return msg_count;
}


char *form_cdr_msg(const fileinfo_t *f, const char *data)
{
	char *msg = NULL;
	const char *prefix = "{srcfile : '%s', size : %lu, data : '";
	const char *suffix = "'}";

	size_t pflen  = strlen(prefix) + strlen(f->path) + 20;
	size_t msglen = pflen + strlen(data) + strlen(suffix) + 1;
	msg = malloc(msglen);
	if(!msg){
		perror("form_cdr_msg: malloc");
		return NULL;
	}
	memset(msg, 0, msglen);
	int pfoff = snprintf(msg, pflen, prefix, f->path, strlen(data));
	memcpy(msg+pfoff, data, strlen(data));
	int sfoff = snprintf(msg+pfoff+strlen(data), strlen(suffix)+1, suffix);

	char *mbuf = realloc(msg, pfoff+strlen(data)+sfoff+1);
	if(!mbuf){
		perror("form_cdr_msg: realloc");
		free(msg);
		return NULL;
	}

	return mbuf;
}


int publish(const char *msg, size_t msg_size)
{
	int ret;
restart:
	ret = rd_kafka_produce(rkt, RD_KAFKA_PARTITION_UA, RD_KAFKA_MSG_F_COPY, (void*)msg, msg_size, NULL, 0, NULL);
	if(ret == -1){
		rd_kafka_resp_err_t err = rd_kafka_last_error();
		if(err == RD_KAFKA_RESP_ERR__QUEUE_FULL){
			size_t outq = rd_kafka_outq_len(rk);
			(void)outq;	// avoid debug warn
#ifdef DEBUG
			fprintf(stderr, "Kafka queue full (%u msgs). Flushing buffer...\n", outq);
#endif
			rd_kafka_flush(rk, 1000);
			rd_kafka_poll(rk, -1);
			outq = rd_kafka_outq_len(rk);
#ifdef DEBUG
			fprintf(stderr, "Kafka queue now holds %u msgs. Retrying...\n", outq);
#endif
			goto restart;
		}else{
			fprintf(stderr, "[!] Failed to produce to topic %s: %s\n", rd_kafka_topic_name(rkt), rd_kafka_err2str(rd_kafka_last_error()));
		}
	}

//	size_t outq = rd_kafka_outq_len(rk);
//	fprintf(stderr, "outq: %u\n", outq);
	rd_kafka_poll(rk, 0);
	return 0;
}


void flush_kafka_buffer(size_t sec)
{
	rd_kafka_flush(rk, sec*1000);
}


size_t publish_cdrqueue(cdrmsg_t **cdrq_head)
{
	size_t cdrcount = 0;
	cdrmsg_t *cdrprev = NULL;
	cdrmsg_t *cdrq	   = *cdrq_head;
	while(cdrq){
		++cdrcount;
		publish(cdrq->msg, strlen(cdrq->msg));
		cdrprev = cdrq;
		cdrq = cdrq->next;
		free_cdrmsg(cdrprev);
		cdrprev = NULL;
	}
	*cdrq_head = NULL;

	return cdrcount;
}

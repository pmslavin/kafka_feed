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

	rd_kafka_conf_set_dr_msg_cb(conf, dr_msg_cb);

	rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errbuf, sizeof(errbuf));

	if(!rk){
		fprintf(stderr, "producer: %s\n", errbuf);
		return -1;
	}

	rkt = rd_kafka_topic_new(rk, topic, NULL);

	int ret = rd_kafka_produce(rkt, RD_KAFKA_PARTITION_UA, RD_KAFKA_MSG_F_COPY, txt, sizeof(txt), NULL, 0, NULL);

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
	rd_kafka_flush(rk, 10*1000);

	rd_kafka_topic_destroy(rkt);
	rd_kafka_destroy(rk);

	return 0;
}

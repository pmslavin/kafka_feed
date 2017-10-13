#ifndef __KAFKAOPS_H__
#define __KAFKAOPS_H__

#include "librdkafka/rdkafka.h"
#include "fileops.h"

int init_kafka_producer(void);
int close_kafka_producer(void);
char *form_msg(const fileinfo_t *, size_t *);
int publish(const char *, size_t);

#endif

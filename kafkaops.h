#ifndef __KAFKAOPS_H__
#define __KAFKAOPS_H__

#include "librdkafka/rdkafka.h"

int init_kafka_producer(void);
int close_kafka_producer(void);

#endif

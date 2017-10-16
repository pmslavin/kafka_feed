#ifndef __KAFKAOPS_H__
#define __KAFKAOPS_H__

#include "librdkafka/rdkafka.h"
#include "fileops.h"

int init_kafka_producer(void);
int close_kafka_producer(void);
char *form_file_msg(const fileinfo_t *, size_t *);
size_t form_cdr_msgs(cdrmsg_t **, const fileinfo_t *);
char *form_cdr_msg(const fileinfo_t *, const char *);
int publish(const char *, size_t);
void flush_kafka_buffer(void);

#endif

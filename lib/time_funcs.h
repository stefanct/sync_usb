#ifndef TIME_FUNCS_H
#define TIME_FUNCS_H

#define NS_PER_SEC 1000000000L
#define US_PER_SEC    1000000L

int print_timeval(struct timeval *tv);
int print_timespec(struct timespec *ts, struct timespec *res);
int timespec2str(char *buf, int len, struct timespec *ts);
int print_time_host(int clk_id);

#endif // TIME_FUNCS_H

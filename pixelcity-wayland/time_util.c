#ifdef WINDOWS
# include <windows.h>
#else
# include <sys/time.h>
# include <time.h>
#endif

#include "time_util.h"

int GetTimeInMillis()
{
#ifdef WINDOWS
  return GetTickCount();
#else
  struct timeval tv;

  gettimeofday(&tv, NULL);

  return tv.tv_usec / 1000 + ((tv.tv_sec % 1000000) * 1000);
#endif
}

double GetTimeInSeconds()
{
#ifdef WINDOWS
  return (double)GetTickCount() / 1000.0;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
#endif
}

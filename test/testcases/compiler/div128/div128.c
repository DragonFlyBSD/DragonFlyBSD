#include <inttypes.h>
#include <stdio.h>

int main(void)
{
  __uint128_t num = 1000;
  __uint128_t denom = 10;
  __uint128_t res = num / denom;
  printf("Result = %ju\n", (uintmax_t)res); 
  return 0;
}


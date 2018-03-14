#ifndef __DEBUG_H__
#define __DEBUG_H__

#define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
# define LOG(str) (printf("%s (%d) - <%s> %s\n", __FILE__ , __LINE__ ,__FUNCTION__, str))
#else
# define dbg_printf(...)
# define LOG(str) 
#endif

#endif
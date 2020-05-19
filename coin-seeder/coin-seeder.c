#include <stdio.h>
#include <liblog/log.h>

int main (int argc, char *argv[])
{
    log_set_level(LOG_TRACE);
    log_trace("Tracer!");
}

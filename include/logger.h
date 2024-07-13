#ifndef _LOGGER_
#define _LOGGER_

void log_set_app_name(const char* name);
void log_info(const char* message, ...);
void log_debug(const char* message, ...);
void log_warning(const char* message, ...);
void log_error(const char* message, ...);

#endif
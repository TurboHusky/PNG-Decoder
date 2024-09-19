// https://cplusplus.com/reference/cstdio/printf/
// %[flags][width][.precision][length]specifier

#include "logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <limits.h>

#define TK_NO_FLAGS 0x00
#define TK_LEFT_JUSTIFY 0x80
#define TK_FORCE_SIGN 0x40
#define TK_NO_SIGN 0x20
#define TK_USE_PREFIX 0x10
#define TK_PAD_ZERO 0x08

#define TK_LEN_NONE 0x00
#define TK_LEN_CHAR 0x01
#define TK_LEN_SHORT 0x02
#define TK_LEN_LONG 0x03
#define TK_LEN_LONG_LONG 0x04
#define TK_LEN_MAX 0x05
#define TK_LEN_SIZE 0x06
#define TK_LEN_PTRDIFF 0x07
#define TK_LEN_LONG_DOUBLE 0x08

#define MAX_STR_LEN 256

#define LOG_READ_SETTINGS 0x00000000
#define LOG_UPDATE_SETTINGS 0x80000000
#define LOG_UPDATE_APP_NAME 0x40000000

#define LOG_MAX_APP_NAME_LENGTH 16
#define LOG_TIMESTAMP_LENGTH 32

#define DEFAULT_FLOAT_PRECISION 6

struct logger_settings_t
{
    char name[LOG_MAX_APP_NAME_LENGTH];
};

struct token_t
{
    int width;
    int precision;
    uint8_t flags;
    uint8_t length;
    char specifier;
};

const char *info_msg = "] [\033[32minfo\033[0m] ";
const char *debug_msg = "] [\033[34mdebug\033[0m] ";
const char *warning_msg = "] [\033[3;33mwarning\033[0m] ";
const char *error_msg = "] [\033[1;31merror\033[0m] ";

static size_t string_copy(char *output, size_t limit, const char *input)
{
    size_t index = 0;
    while (input[index] != '\0' && index < limit)
    {
        output[index] = input[index];
        ++index;
    }
    return index;
}

static struct logger_settings_t *logger_settings(uint32_t mode, void *settings)
{
    static struct logger_settings_t logger_settings = {0};
    if (mode & LOG_UPDATE_SETTINGS && mode & LOG_UPDATE_APP_NAME && settings != NULL)
    {
        const char *app_name = (const char *)settings;
        size_t app_name_len = strlen(app_name);
        if (app_name_len > LOG_MAX_APP_NAME_LENGTH)
        {
            app_name_len = LOG_MAX_APP_NAME_LENGTH;
        }
        string_copy(logger_settings.name, app_name_len, app_name);
    }

    return &logger_settings;
}

static size_t readtoken(const char *input, struct token_t *token)
{
    size_t len = strlen(input);
    size_t index = 0;
    char in = '0';

    if (len == 0)
    {
        return 0;
    }

    // flags
    uint8_t f = TK_NO_FLAGS;
    while (index < len)
    {
        in = *(input + index);
        if (in == '-')
        {
            f |= TK_LEFT_JUSTIFY;
        }
        else if (in == '+')
        {
            f |= TK_FORCE_SIGN;
        }
        else if (in == ' ')
        {
            f |= TK_NO_SIGN;
        }
        else if (in == '#')
        {
            f |= TK_USE_PREFIX;
        }
        else if (in == '0')
        {
            f |= TK_PAD_ZERO;
        }
        else
        {
            break;
        }
        ++index;
    }
    token->flags = f;

    // width
    int width = 0;
    while (index < len)
    {
        in = *(input + index);
        if (in >= '0' && in <= '9')
        {
            width = width * 10 + (in - '0');
        }
        else if (in == '*')
        {
            ++index;
            break;
        }
        else
        {
            break;
        }
        ++index;
    }
    token->width = width;

    if (index >= len)
    {
        return index;
    }

    // precision
    int precision = 0;
    in = *(input + index);
    if (in == '.')
    {
        ++index;
        while (index < len)
        {
            in = *(input + index);
            if (in == '*')
            {
                ++index;
                break;
            }
            if (in >= '0' && in <= '9')
            {
                precision = precision * 10 + (in - '0');
            }
            else
            {
                break;
            }
            ++index;
        }
        token->precision = precision;
    }

    // length
    uint8_t length = TK_LEN_NONE;
    in = *(input + index);
    switch (in)
    {
    case 'h':
        length = TK_LEN_CHAR;
        if ((index + 1) < len && *(input + index + 1) == 'h')
        {
            length = TK_LEN_SHORT;
            ++index;
        }
        break;
    case 'l':
        length = TK_LEN_LONG_LONG;
        if ((index + 1) < len && *(input + index + 1) == 'l')
        {
            length = TK_LEN_LONG;
            --index;
        }
        break;
    case 'j':
        length = TK_LEN_MAX;
        break;
    case 'z':
        length = TK_LEN_SIZE;
        break;
    case 't':
        length = TK_LEN_PTRDIFF;
        break;
    case 'L':
        length = TK_LEN_LONG_DOUBLE;
        break;
    default:
        index--;
        break;
    }
    ++index;
    token->length = length;

    // specifier
    token->specifier = '0';
    if (index < len)
    {
        token->specifier = *(input + index);
        ++index;
    }
    return index - 1;
}

size_t convert_decimal(unsigned int n, char *output_buffer, size_t width, char padchar)
{
    char buffer[MAX_STR_LEN];
    size_t buffer_size = 0;
    size_t index = 0;
    if (n == 0)
    {
        output_buffer[index] = '0';
        return index + 1;
    }
    while (n)
    {
        buffer[buffer_size] = '0' + (char)(n % 10);
        ++buffer_size;
        n = n / 10;
    }
    while (width > buffer_size)
    {
        output_buffer[index] = padchar;
        ++index;
        --width;
    }
    while (buffer_size > 0)
    {
        --buffer_size;
        output_buffer[index] = buffer[buffer_size];
        ++index;
    }
    return index;
}

size_t convert_double(float n, uint32_t precision, char *output_buffer)
{
    size_t index = 0;
    uint64_t main = (uint64_t)n;
    if (n < 0)
    {
        output_buffer[index] = '-';
        ++index;
        n *= -1;
    }
    index = convert_decimal(main, output_buffer, 0, '0');
    n -= main;

    if (n > 0)
    {
        output_buffer[index] = '.';
        ++index;
    }

    uint64_t temp;
    while (n > 0 && precision > 0)
    {
        n *= 10;
        temp = (uint64_t)n;
        output_buffer[index] = '0' + (char)(temp);
        ++index;
        n -= temp;
        --precision;
    }

    return index;
}

size_t parse_message(char *output_buffer, size_t output_buffer_size, const char *message, va_list *args)
{
    size_t len = strlen(message);
    size_t output_index = 0;
    if (len > MAX_STR_LEN)
    {
        fputs("String is too long\n", stdout);
        return 0;
    }

    size_t index = 0;
    while (index < len && output_index < output_buffer_size)
    {
        char in = *(message + index);
        if (in != '%')
        {
            output_buffer[output_index] = in;
            ++output_index;
        }
        else
        {
            ++index;
            struct token_t token = {0};
            int delta = readtoken(message + index, &token);
            const char padchar = (token.flags & TK_PAD_ZERO) ? '0' : ' ';
            int i;
            unsigned int u;
            double d;
            (void)d;
            char *string;
            char buffer[MAX_STR_LEN];
            int buffer_size;

            switch (token.specifier)
            {
            case 'c':
                output_buffer[output_index] = va_arg(*args, int);
                ++output_index;
                break;
            case 's':
                string = va_arg(*args, char *);
                if (strlen(string) <= MAX_STR_LEN) // Fix this, should check remaining buffer size
                {
                    strcpy(output_buffer + output_index, string);
                    output_index += strlen(string);
                }
                break;
            case '%':
                output_buffer[output_index] = '%';
                ++output_index;
                break;
            case 'd':
            case 'i':
                i = va_arg(*args, int);
                if (i < 0)
                {
                    output_buffer[output_index] = '-';
                    ++output_index;
                    i = -i;
                }
                else if (token.flags & TK_FORCE_SIGN)
                {
                    output_buffer[output_index] = '+';
                    ++output_index;
                }
                output_index += convert_decimal(i, output_buffer + output_index, token.width, padchar);
                break;
            case 'u':
                u = va_arg(*args, unsigned int);
                if (token.flags & TK_FORCE_SIGN)
                {
                    output_buffer[output_index] = '+';
                    ++output_index;
                }
                output_index += convert_decimal(u, output_buffer + output_index, token.width, padchar);
                break;
            case 'x':
            case 'X':
                u = va_arg(*args, unsigned int);
                buffer_size = 0;
                if (token.flags & TK_USE_PREFIX)
                {
                    buffer[buffer_size] = '0';
                    buffer[buffer_size + 1] = token.specifier;
                    ++buffer_size;
                }
                if (u == 0)
                {
                    buffer[buffer_size] = '0';
                    buffer[buffer_size + 1] = '0';
                    buffer_size += 2;
                }
                while (u)
                {
                    buffer[buffer_size] = (u & 0x0000000F) + '0';
                    if (buffer[buffer_size] > '9')
                    {
                        buffer[buffer_size] += (token.specifier) - 81;
                    }
                    u >>= 4;
                    ++buffer_size;
                }
                while (token.width > buffer_size)
                {
                    output_buffer[output_index] = padchar;
                    ++output_index;
                    --token.width;
                }
                while (buffer_size)
                {
                    --buffer_size;
                    output_buffer[output_index] = buffer[buffer_size];
                    ++output_index;
                }
                break;
            case 'f':
            case 'F':
                d = va_arg(*args, double);
                output_index += convert_double(d, (token.precision) ? token.precision : DEFAULT_FLOAT_PRECISION, output_buffer + output_index);
                break;
            case 'e':
            case 'E':
                d = va_arg(*args, double);
                fputs("exp", stdout);
                break;
            case 'o':
                u = va_arg(*args, unsigned int);
                buffer_size = 0;
                if (u == 0)
                {
                    buffer[buffer_size] = '0';
                    ++buffer_size;
                }
                while (u)
                {
                    buffer[buffer_size] = (u & 0x00000007) + '0';
                    u >>= 3;
                    ++buffer_size;
                }
                if (token.flags & TK_USE_PREFIX)
                {
                    buffer[buffer_size] = '0';
                    ++buffer_size;
                }
                while (token.width > buffer_size)
                {
                    output_buffer[output_index] = padchar;
                    ++output_index;
                    --token.width;
                }
                while (buffer_size)
                {
                    --buffer_size;
                    output_buffer[output_index] = buffer[buffer_size];
                    ++output_index;
                }
                break;
            case 'g':
            case 'G':
                d = va_arg(*args, double);
                fputs("short", stdout);
                break;
            case 'a':
            case 'A':
                d = va_arg(*args, double);
                fputs("float", stdout);
                break;
            case 'p':
                u = va_arg(*args, unsigned int);
                fputs("ptr", stdout);
                break;
            case 'n':
                i = va_arg(*args, int);
                fputs("store", stdout);
                break;
            default:
                --index;
                break;
            }
            index += delta;
        }
        ++index;
    }

    return output_index;
}

static size_t add_timestamp(char *timebuffer)
{
    time_t ltime = time(NULL);
    struct tm *time2 = localtime(&ltime);
    strftime(timebuffer, 32, "%Y-%m-%dT%H:%M:%S", time2);
    size_t index = strlen(timebuffer);

    struct timespec now;
    timespec_get(&now, TIME_UTC);

    timebuffer[index] = '.';
    ++index;
    index += convert_decimal(now.tv_nsec / 1e6, timebuffer + index, 3, '0');
    timebuffer[index] = 'Z';
    ++index;
    timebuffer[index] = '\0';

    return index;
}

void log_set_app_name(const char *name)
{
    logger_settings(LOG_UPDATE_SETTINGS | LOG_UPDATE_APP_NAME, (void *)name);
}

void logger(const char *type, const char *message, va_list *args)
{
    char output[MAX_STR_LEN] = {'\0'};
    size_t index = add_timestamp(output);

    output[index] = ' ';
    output[index + 1] = '[';
    index += 2;
    const char *app_name = logger_settings(LOG_READ_SETTINGS, NULL)->name;

    index += string_copy(output + index, strlen(app_name), app_name);
    index += string_copy(output + index, MAX_STR_LEN - index, type);
    index += parse_message(output + index, MAX_STR_LEN - index, message, args);
    output[index] = '\n';
    output[index + 1] = '\0';
    fputs(output, stdout);
}

void log_info(const char *message, ...)
{
    va_list args;
    va_start(args, message);
    logger(info_msg, message, &args);
    va_end(args);
}

void log_debug(const char *message, ...)
{
#ifdef LOG_DEBUG
    va_list args;
    va_start(args, message);
    logger(debug_msg, message, &args);
    va_end(args);
#else
    (void)message;
#endif
}

void log_warning(const char *message, ...)
{
    va_list args;
    va_start(args, message);
    logger(warning_msg, message, &args);
    va_end(args);
}

void log_error(const char *message, ...)
{
    va_list args;
    va_start(args, message);
    logger(error_msg, message, &args);
    va_end(args);
}
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

#include <string.h>

#include <signal.h>

#define NME_VERSION_STRING "0.1"
#define NME_BUILD_FEATURES "unpack:dump"

#define NME_TRUE 1
#define NME_FALSE 0

#define NME_BACKSLASH '\\'
#define NME_FORWARD_SLASH '/'

#define NME_PATH_SEPARATOR NME_FORWARD_SLASH

#define NME_STRINGIFY(MACRO) #MACRO
#define NME_EXPAND_AND_STRINGIFY(MACRO) NME_STRINGIFY(MACRO)

#define NME_ASSERT(CONDITION) \
    do { \
        if ((CONDITION) == NME_FALSE) { \
            die("assertion failed in " __FILE__ " at line " \
                NME_EXPAND_AND_STRINGIFY(__LINE__) " for `" #CONDITION "`"); \
        } \
    } while (NME_FALSE)

typedef struct entry entry_t;
typedef struct queue queue_t;

struct entry {
    char name[32];
    int8_t type;

    uint8_t unused[3];

    uint32_t size;
    uint32_t offset;
};

struct queue {
    size_t head;
    size_t tail;

    size_t size;
    size_t capacity;

    entry_t *data;
};

static char const *NME_EXECUTABLE_NAME = NULL;
static char const *NME_INPUT_FILENAME = NULL;

static void report(char const *message, ...)
{
    if (message == NULL) {
        fprintf(stderr, "%s: unknown error\n", NME_EXECUTABLE_NAME);
        return;
    }

    va_list arguments;
    va_start(arguments, message);

    char *buffer = malloc(1024);

    vsnprintf(buffer, 1024, message, arguments);
    fprintf(stderr, "%s: %s\n", NME_EXECUTABLE_NAME, buffer);

    free(buffer);
    va_end(arguments);
}

static void fail(char const *message, ...)
{
    va_list arguments;
    va_start(arguments, message);

    report(message, arguments);

    va_end(arguments);
    exit(EXIT_FAILURE);
}

static void die(char const *message, ...)
{
    va_list arguments;
    va_start(arguments, message);

    report(message, arguments);

    va_end(arguments);
    abort();
}

void handle_signal(int signal_identifier)
{
    (void) signal_identifier;

    fprintf(stderr, "%s: aborting\n", NME_EXECUTABLE_NAME);
    exit(EXIT_FAILURE);
}

static void *allocate(size_t size)
{
    void *result = malloc(size);

    if (result == NULL) {
        die("malloc(%lu) failed", size);
    }

    return result;
}

static void *read(FILE *file, void *buffer, size_t size)
{
    if (file == NULL || ferror(file) != NME_FALSE) {
        die("invalid or corrupt file");
    } else if (buffer == NULL) {
        die("invalid or corrupt buffer");
    }

    size_t count = fread(buffer, size, 1, file);

    if (count != 1) {
        report("read(%lu) failed", size);
    }

    return buffer;
}

static void write(FILE *file, void const *buffer, size_t size)
{
    if (file == NULL || ferror(file) != 0) {
        die("invalid or corrupt file");
    } else if (buffer == NULL) {
        die("invalid or corrupt buffer");
    }

    size_t count = fwrite(buffer, size, 1, file);

    if (count != 1) {
        report("write(0x%08x, %lu) failed", buffer, size);
    }
}

static queue_t *create_queue(size_t capacity)
{
    NME_ASSERT(capacity >= 1);

    queue_t *queue = allocate(sizeof (queue_t));
    memset(queue, 0x00, sizeof (queue_t));

    queue->data = allocate(sizeof (entry_t) * capacity);

    queue->capacity = capacity;
    queue->tail = queue->capacity - 1;

    return queue;
}

static void free_queue(queue_t *queue)
{
    if (queue != NULL) {
        free(queue->data);
    }

    memset(queue, 0x00, sizeof (queue_t));
    free(queue);
}

static void enqueue(queue_t *queue, entry_t const *entry)
{
    NME_ASSERT(queue != NULL || queue->data != NULL);
    NME_ASSERT(queue->size + 1 < queue->capacity);

    queue->tail = (queue->tail + 1) % queue->capacity;
    memmove(queue->data + queue->tail, entry, sizeof (entry_t));

    ++queue->size;
}

static entry_t const *dequeue(queue_t *queue)
{
    NME_ASSERT(queue != NULL || queue->data != NULL);

    entry_t const *entry = queue->data + queue->head;
    queue->head = (queue->head + 1) % queue->capacity;

    --queue->size;

    return entry;
}

static int is_queue_empty(queue_t const *queue)
{
    NME_ASSERT(queue != NULL);
    return (queue->size == 0);
}

static void fix_path_separators(char *input)
{
    for (; input != NULL; input = strchr(input, NME_BACKSLASH)) {
        *input = NME_PATH_SEPARATOR;
    }
}

static char const *extract_executable_name(char *filename)
{
    char *executable_name = NULL;

    fix_path_separators(filename);
    executable_name = strrchr(filename, NME_PATH_SEPARATOR);

    if (executable_name == NULL || *(executable_name + 1) == '\0') {
        return filename;
    }

    return ++executable_name;
}

static void display_help_screen(void)
{
    printf(
        "                                             888\n                   "
        "                          888\n                                      "
        "       888\n888  888 88888b.  88888b.   8888b.   .d8888b 888  888  .d"
        "88b.  888d888\n888  888 888 \"88b 888 \"88b     \"88b d88P\"    888 ."
        "88P d8P  Y8b 888P\"\n888  888 888  888 888  888 .d888888 888      888"
        "888K  88888888 888\nY88b 888 888  888 888 d88P 888  888 Y88b.    888 "
        "\"88b Y8b.     888\n \"Y88888 888  888 88888P\"  \"Y888888  \"Y8888P "
        "888  888  \"Y8888  888\n                  888\n                  888"
        "\n                  888\n"
        "\n"
        "Usage:\n"
        "        %s [options] file...\n"
        "\n"
        "Options:\n"
        "        -h                  display this help screen\n"
        "        -v                  display version information\n"
        "\n",
        NME_EXECUTABLE_NAME);
}

static void display_version_information(void)
{
    printf("nme-unpacker (%s) version %s [%s]\nauthored in 2018 $ released int"
        "o the public domain\n", NME_EXECUTABLE_NAME, NME_VERSION_STRING,
        NME_BUILD_FEATURES);
}

static void handle_command_line_option(char option, char const *argument)
{
    switch (option) {
    case 'h':
        display_help_screen();
        break;

    case 'v':
        display_version_information();
        break;

    default:
        report("unknown option `%c`", option);
    }
}

static void parse_command_line(int count, char **arguments)
{
    for (int i = 1; i < count; ++i) {
        char const *argument = arguments[i];
        char const *parameters = NULL;

        switch (argument[0]) {
        case '-':
            if (argument[2] != '\0') {
                parameters = argument + 2;
            }

            handle_command_line_option(argument[1], parameters);
            break;

        default:
            if (NME_INPUT_FILENAME != NULL) {
                report("overriding input filename");
            }

            NME_INPUT_FILENAME = arguments[i];
        }
    }
}

int main(int count, char *arguments[])
{
    NME_EXECUTABLE_NAME = extract_executable_name(*arguments);

    signal(SIGABRT, handle_signal);
    signal(SIGINT, handle_signal);

    parse_command_line(count, arguments);

    if (count == 1 || NME_INPUT_FILENAME == NULL) {
        fail("no input files");
    }

    return EXIT_SUCCESS;
}

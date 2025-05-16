#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <mariadb/mysql.h>

// GPIO 핀 번호 및 상태 정의
#define IN 0
#define OUT 1
#define LOW 0
#define HIGH 1
#define PIN 20 // 입력 핀
#define POUT2 21 // 출력 핀
#define VALUE_MAX 40
#define DIRECTION_MAX 40

// MariaDB 연결 정보 정의
#define HOST "localhost" // 데이터베이스 호스트
#define USER "root"      // 사용자 이름
#define PASS "7179"      // 비밀번호
#define DB "domitory_db" // 데이터베이스 이름

// 스레드 인자 구조체 정의
typedef struct {
    void *arg;    // 추가 인자
    MYSQL *conn;  // MariaDB 연결 객체
} thread_args_t;

// MariaDB 쿼리 실행 함수
void execute_query(MYSQL *conn, const char *query) {
    if (mysql_query(conn, query)) {
        fprintf(stderr, "Query execution error: %s\n", mysql_error(conn));
        return;
    }

    MYSQL_RES *result = mysql_store_result(conn);
    if (!result) {
        fprintf(stderr, "Error storing result: %s\n", mysql_error(conn));
        return;
    }

    MYSQL_ROW row;
    MYSQL_FIELD *fields;
    unsigned int num_fields = mysql_num_fields(result);

    fields = mysql_fetch_fields(result);
    for (unsigned int i = 0; i < num_fields; i++) {
        printf("%s\t", fields[i].name);
    }
    printf("\n");

    while ((row = mysql_fetch_row(result))) {
        for (unsigned int i = 0; i < num_fields; i++) {
            printf("%s\t", row[i] ? row[i] : "NULL");
        }
        printf("\n");
    }

    mysql_free_result(result);
}

// GPIO 핀을 export하는 함수
static int GPIOExport(int pin) {
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open export for writing!\n");
        return -1;
    }

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return 0;
}

// GPIO 핀을 unexport하는 함수
static int GPIOUnexport(int pin) {
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open unexport for writing!\n");
        return -1;
    }

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return 0;
}

// GPIO 핀 방향 설정 함수
static int GPIODirection(int pin, int dir) {
    static const char s_directions_str[] = "in\0out";

    char path[DIRECTION_MAX];
    int fd;

    snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
    fd = open(path, O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open gpio direction for writing!\n");
        return -1;
    }

    if (-1 == write(fd, &s_directions_str[IN == dir ? 0 : 3], IN == dir ? 2 : 3)) {
        fprintf(stderr, "Failed to set direction!\n");
        return -1;
    }

    close(fd);
    return 0;
}

// GPIO 값을 읽는 함수
static int GPIORead(int pin) {
    char path[VALUE_MAX];
    char value_str[3];
    int fd;

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_RDONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open gpio value for reading!\n");
        return -1;
    }

    if (-1 == read(fd, value_str, 3)) {
        fprintf(stderr, "Failed to read value!\n");
        return -1;
    }

    close(fd);
    return atoi(value_str);
}

// GPIO 값을 쓰는 함수
static int GPIOWrite(int pin, int value) {
    static const char s_values_str[] = "01";

    char path[VALUE_MAX];
    int fd;

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open gpio value for writing!\n");
        return -1;
    }

    if (1 != write(fd, &s_values_str[LOW == value ? 0 : 1], 1)) {
        fprintf(stderr, "Failed to write value!\n");
        return -1;
    }

    close(fd);
    return 0;
}

// 전역 변수: 프로그램 실행 여부를 나타내는 플래그
atomic_int running = 1;

// GPIO 관련 작업을 처리하는 스레드 함수
void *gpio_thread(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    MYSQL *conn = args->conn;

    if (GPIOWrite(POUT2, 1) == -1) {
        fprintf(stderr, "GPIOWrite failed!\n");
        atomic_store(&running, 0);
        return NULL;
    }

    do {
        if (GPIOWrite(POUT2, 1) == -1) {
            return NULL;
        }

        if (GPIORead(PIN) == 1) {
            execute_query(conn, "SELECT * FROM students;"); // MariaDB 쿼리 실행
        }
        usleep(1000 * 1000); // 1초 대기
    } while (atomic_load(&running));

    usleep(500000);
    return NULL;
}

// 사용자 입력을 처리하는 스레드 함수
void *input_thread(void *arg) {
    char input[100];
    while (atomic_load(&running)) {
        printf("Enter 'exit' to quit: ");
        if (fgets(input, sizeof(input), stdin) != NULL) {
            input[strcspn(input, "\n")] = 0; // 개행 문자 제거
            if (strcmp(input, "exit") == 0) {
                printf("Exiting program...\n");
                atomic_store(&running, 0); // atomic_int running=0으로 하여 다른 thread 종료
            }
        }
    }

    return NULL;
}

// 메인 함수
int main(int argc, char* argv[]) {
    thread_args_t *args = malloc(sizeof(thread_args_t));
    if (args == NULL) {
        perror("Failed to allocate memory for thread arguments");
        exit(1);
    }

    printf("Starting program...\n");

    MYSQL *conn = mysql_init(NULL); // MariaDB 초기화
    args->conn = conn;
    args->arg = NULL;

    if (conn == NULL) {
        fprintf(stderr, "mysql_init() failed\n");
        return 1;
    }

    if (mysql_real_connect(conn, HOST, USER, PASS, DB, 0, NULL, 0) == NULL) {
        fprintf(stderr, "mysql_real_connect() failed: %s\n", mysql_error(conn));
        mysql_close(conn);
        return 1;
    }

    printf("Connected to MariaDB database '%s'.\n", DB);

    pthread_t gpio_tid, input_tid;

    if (pthread_create(&gpio_tid, NULL, gpio_thread, (void*)args) != 0) { // gpio_thread 생성
        perror("pthread_create for gpio_thread failed");
        free(args);
        return 1;
    }

    if (pthread_create(&input_tid, NULL, input_thread, NULL) != 0) { // input_thread 생성
        perror("pthread_create for input_thread failed");
        atomic_store(&running, 0);
        pthread_join(gpio_tid, NULL);
        return 1;
    }

    if (GPIOExport(POUT2) == -1 || GPIOExport(PIN)) {
        return 1;
    }

    if (GPIODirection(POUT2, OUT) == -1 || GPIODirection(PIN, IN) == -1) {
        return 2;
    }

    pthread_join(gpio_tid, NULL);
    pthread_join(input_tid, NULL);

    if (GPIOUnexport(POUT2) == -1 || GPIOUnexport(PIN) == -1) {
        return 4;
    }

    mysql_close(conn); // MariaDB 연결 종료
    printf("Program terminated.\n");
    return 0;
}

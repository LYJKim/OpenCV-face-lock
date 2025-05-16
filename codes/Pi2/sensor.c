#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>

// GPIO 핀 번호 정의
#define RED 17             // 빨간색 LED 핀 번호
#define GREEN 27           // 초록색 LED 핀 번호
#define BLUE 22            // 파란색 LED 핀 번호
#define BUZZER_PIN 18      // 부저 핀 번호

// TCP 및 UDP 서버 설정
#define PORT 22222         // TCP 서버 포트
#define BUFFER_SIZE 1024   // TCP 메시지 버퍼 크기
#define PORT2 54321        // UDP 핑 체크 포트
#define TIMEOUT_SEC 2      // UDP 응답 타임아웃 (초)

// PING 및 응답 처리 설정
#define MAX_PIES 3         // 모니터링할 장치 수
#define MAX_MISS 3         // 누락된 PING의 최대 허용 횟수

// ping 메시지를 보낼 타겟 장치 IP 주소 배열
char *targets[] = {"192.168.65.3", "192.168.65.5", "192.168.65.10"};
int missed_pings[MAX_PIES] = {0}; // 각 장치별 누락된 PING 횟수
int alive[MAX_PIES] = {1, 1, 1};  // 각 장치별 상태 (1: 활성, 0: 비활성)

// 프로그램 실행 상태를 나타내는 플래그
atomic_int running = 1;

// GPIO 핀을 초기화하는 함수
void gpio_export(int pin) {
    FILE *fp = fopen("/sys/class/gpio/export", "w"); // GPIO export 파일 열기
    if (fp == NULL) {
        perror("Failed to open export"); // 파일 열기 실패 시 에러 출력
        exit(1); // 프로그램 종료
    }
    fprintf(fp, "%d", pin); // 핀 번호를 파일에 기록하여 GPIO 활성화
    fclose(fp); // 파일 닫기
}

// GPIO 핀을 비활성화하는 함수
void gpio_unexport(int pin) {
    FILE *fp = fopen("/sys/class/gpio/unexport", "w"); // GPIO unexport 파일 열기
    if (fp == NULL) {
        perror("Failed to open unexport"); // 파일 열기 실패 시 에러 출력
        exit(1); // 프로그램 종료
    }
    fprintf(fp, "%d", pin); // 핀 번호를 파일에 기록하여 GPIO 비활성화
    fclose(fp); // 파일 닫기
}

// GPIO 핀의 방향을 설정하는 함수
void gpio_set_direction(int pin, const char *direction) {
    char path[35]; // 파일 경로 버퍼
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin); // 핀의 방향 설정 경로 생성

    FILE *fp = fopen(path, "w"); // 방향 설정 파일 열기
    if (fp == NULL) {
        perror("Failed to set direction"); // 파일 열기 실패 시 에러 출력
        exit(1); // 프로그램 종료
    }
    fprintf(fp, "%s", direction); // 방향 ("in" 또는 "out") 설정
    fclose(fp); // 파일 닫기
}

// GPIO 핀에 값을 쓰는 함수
void gpio_write_value(int pin, int value) {
    char path[35]; // 파일 경로 버퍼
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin); // 핀의 값 설정 경로 생성

    FILE *fp = fopen(path, "w"); // 값 설정 파일 열기
    if (fp == NULL) {
        perror("Failed to write value"); // 파일 열기 실패 시 에러 출력
        exit(1); // 프로그램 종료
    }
    fprintf(fp, "%d", value); // 핀에 값 (0 또는 1) 쓰기
    fclose(fp); // 파일 닫기
}

// LED를 켜고 끄는 함수
void turn_on(const char *color) {
    // 색상에 따라 해당 LED 켜고 1초 대기 후 끄기
    if (strcmp(color, "RED") == 0) {
        gpio_write_value(RED, 0);  // 빨간색 LED 켜기
        sleep(1);                 // 1초 대기
        gpio_write_value(RED, 1); // 빨간색 LED 끄기
    } else if (strcmp(color, "GREEN") == 0) {
        gpio_write_value(GREEN, 0); // 초록색 LED 켜기
        sleep(1);                   // 1초 대기
        gpio_write_value(GREEN, 1); // 초록색 LED 끄기
    } else if (strcmp(color, "BLUE") == 0) {
        gpio_write_value(BLUE, 0);  // 파란색 LED 켜기
        sleep(1);                   // 1초 대기
        gpio_write_value(BLUE, 1);  // 파란색 LED 끄기
    } else { // 두 개의 LED를 동시에 켜는 YELLOW 상태
        gpio_write_value(GREEN, 0); // 초록색 LED 켜기
        gpio_write_value(RED, 0);   // 빨간색 LED 켜기
        sleep(1);                   // 1초 대기
        gpio_write_value(GREEN, 1); // 초록색 LED 끄기
        gpio_write_value(RED, 1);   // 빨간색 LED 끄기
    }
}

// 라즈베리 파이에 UDP PING 메시지를 전송하는 함수
void ping_pies(int sockfd, struct sockaddr_in *addresses) {
    char *ping_msg = "PING"; // PING 메시지 정의
    socklen_t addr_len = sizeof(struct sockaddr_in);

    for (int i = 0; i < MAX_PIES; i++) {
        // UDP를 통해 각 타겟에 PING 메시지 전송
        if (sendto(sockfd, ping_msg, strlen(ping_msg), 0, (struct sockaddr *)&addresses[i], addr_len) < 0) {
            perror("sendto"); // 오류 발생 시 출력
        }
    }
}

// UDP 응답을 수신하고 상태를 업데이트하는 함수
void listen_for_responses(int sockfd) {
    char buffer[64];                  // 응답 메시지를 저장할 버퍼
    socklen_t addr_len = sizeof(struct sockaddr_in);
    struct sockaddr_in sender_addr;   // 응답을 보낸 장치 주소
    fd_set read_fds;                  // 읽기 파일 디스크립터 설정
    struct timeval timeout;           // 응답 타임아웃 설정
    int responded[MAX_PIES] = {0};    // 각 타겟 장치의 응답 여부 기록

    timeout.tv_sec = TIMEOUT_SEC; // 타임아웃 시간 (초)
    timeout.tv_usec = 0;

    FD_ZERO(&read_fds); // 읽기 파일 디스크립터 초기화
    FD_SET(sockfd, &read_fds); // 소켓 추가

    // 응답 수신 대기
    int activity = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
    if (activity > 0) {
        while (atomic_load(&running)) {
            if (!FD_ISSET(sockfd, &read_fds)) break; // 타임아웃 발생 시 종료

            // 응답 수신
            ssize_t bytes_received = recvfrom(sockfd, buffer, sizeof(buffer) - 1, MSG_DONTWAIT, 
                                             (struct sockaddr *)&sender_addr, &addr_len);
            if (bytes_received <= 0) break; // 수신 실패

            buffer[bytes_received] = '\0'; // 메시지 종료
            printf("Received: %s\n", buffer); // 수신 메시지 출력

            // 응답이 타겟 중 하나에서 온 경우 처리
            for (int i = 0; i < MAX_PIES; i++) {
                if (strcmp(inet_ntoa(sender_addr.sin_addr), targets[i]) == 0) {
                    printf("Response from %s: %s\n", targets[i], buffer);
                    responded[i] = 1;        // 응답 기록
                    missed_pings[i] = 0;    // 누락 핑 초기화
                    break;
                }
            }
        }
    }

    // 응답 누락된 타겟 처리
    for (int i = 0; i < MAX_PIES; i++) {
        if (!responded[i]) { // 응답 없는 타겟
            missed_pings[i]++;
            if (missed_pings[i] >= MAX_MISS) { // 누락 핑 초과 시 경고
                if (alive[i]) {
                    printf("ALERT: %s is dead!\n", targets[i]);
                    alive[i] = 0; // 타겟 상태 변경
                }
            } else {
                printf("%s missed %d pings\n", targets[i], missed_pings[i]);
            }
        } else if (!alive[i]) { // 오프라인 상태의 타겟이 다시 응답할 경우
            printf("INFO: %s is back online!\n", targets[i]);
            alive[i] = 1; // 상태 복구
        }
    }
}

// 사용자 입력을 처리하는 스레드
void *input_thread(void *arg) {
    char input[100]; // 사용자 입력을 저장할 버퍼
    while (atomic_load(&running)) { // 프로그램 실행 중일 때만 동작
        printf("Enter 'exit' to quit: "); // 종료 명령 안내 출력
        if (fgets(input, sizeof(input), stdin) != NULL) { // 사용자 입력을 읽음
            input[strcspn(input, "\n")] = 0; // 개행 문자 제거
            if (strcmp(input, "exit") == 0) { // 입력이 'exit'인 경우
                printf("Exiting program...\n");
                atomic_store(&running, 0); // 프로그램 실행 상태를 종료로 설정
            }
        }
    }
    return NULL; // 스레드 종료
}

// UDP 연결을 관리하는 스레드
void *connection_thread(void *arg) {
    int sockfd; // UDP 소켓 파일 디스크립터
    struct sockaddr_in my_addr, target_addrs[MAX_PIES]; // 본인 및 타겟 주소 정보

    // UDP 소켓 생성
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket"); // 소켓 생성 실패 시 오류 출력
        exit(EXIT_FAILURE); // 프로그램 종료
    }

    // 본인 주소 초기화
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;         // IPv4
    my_addr.sin_addr.s_addr = INADDR_ANY; // 모든 IP 주소 허용
    my_addr.sin_port = htons(PORT2);      // 지정된 포트 사용

    // 소켓 바인딩
    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
        perror("bind"); // 바인딩 실패 시 오류 출력
        close(sockfd);  // 소켓 닫기
        exit(EXIT_FAILURE); // 프로그램 종료
    }

    // 타겟 주소 초기화
    for (int i = 0; i < MAX_PIES; i++) {
        memset(&target_addrs[i], 0, sizeof(target_addrs[i])); // 구조체 초기화
        target_addrs[i].sin_family = AF_INET;                 // IPv4
        target_addrs[i].sin_port = htons(PORT2);              // 동일 포트 사용
        inet_pton(AF_INET, targets[i], &target_addrs[i].sin_addr); // IP 주소 변환 및 설정
    }

    // PING 메시지를 지속적으로 전송하고 응답을 처리
    while (atomic_load(&running)) { // 프로그램 실행 중인 경우
        ping_pies(sockfd, target_addrs); // PING 메시지 전송
        listen_for_responses(sockfd);   // 응답 수신 및 처리
        sleep(5); // 5초 대기 후 반복
    }

    close(sockfd); // 소켓 닫기
    return NULL; // 스레드 종료
}

// GPIO를 제어하고 Java 서버로부터 메시지를 수신하는 스레드
void *gpio_thread(void *arg) {
    int server_fd, new_socket; // TCP 서버 소켓 및 클라이언트 소켓 파일 디스크립터
    struct sockaddr_in address; // 서버 주소 구조체
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0}; // 클라이언트로부터의 메시지를 저장할 버퍼

    // GPIO 핀 설정
    gpio_export(RED);      // 빨간색 LED 핀 활성화
    gpio_export(GREEN);    // 초록색 LED 핀 활성화
    gpio_export(BLUE);     // 파란색 LED 핀 활성화
    gpio_export(BUZZER_PIN); // 부저 핀 활성화

    gpio_set_direction(RED, "out");    // 빨간색 LED 핀 출력 설정
    gpio_set_direction(GREEN, "out");  // 초록색 LED 핀 출력 설정
    gpio_set_direction(BLUE, "out");   // 파란색 LED 핀 출력 설정
    gpio_set_direction(BUZZER_PIN, "out"); // 부저 핀 출력 설정

    gpio_write_value(BUZZER_PIN, 0); // 부저 초기화
    sleep(1); // 1초 대기
    gpio_write_value(BUZZER_PIN, 1); // 부저 OFF

    // TCP 소켓 생성
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed"); // 소켓 생성 실패 시 오류 출력
        exit(EXIT_FAILURE); // 프로그램 종료
    }

    // 서버 주소 초기화
    address.sin_family = AF_INET;         // IPv4
    address.sin_addr.s_addr = INADDR_ANY; // 모든 IP 주소 허용
    address.sin_port = htons(PORT);       // 지정된 포트 사용

    // 소켓 바인딩
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Binding failed"); // 바인딩 실패 시 오류 출력
        exit(EXIT_FAILURE); // 프로그램 종료
    }

    // 소켓 대기열 설정
    if (listen(server_fd, 3) < 0) {
        perror("Listening failed"); // 대기열 설정 실패 시 오류 출력
        exit(EXIT_FAILURE); // 프로그램 종료
    }

    printf("Sensor server listening on port %d...\n", PORT);

    // 클라이언트 연결 처리 루프
    while (atomic_load(&running)) { // 프로그램 실행 중인 경우
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen); // 클라이언트 연결 수락
        int valread = read(new_socket, buffer, BUFFER_SIZE); // 클라이언트 메시지 읽기
        buffer[strcspn(buffer, "\n")] = '\0'; // 개행 문자 제거

        if (valread > 0) { // 메시지가 수신된 경우
            printf("Message from Java: %s\n", buffer); // 수신 메시지 출력
            if (strcmp(buffer, "1") == 0) { // 메시지가 "1"인 경우
                printf("Yellow LED action\n");
                turn_on("YELLOW"); // 노란색 LED 동작
            } else if (strcmp(buffer, "2") == 0) { // 메시지가 "2"인 경우
                printf("Red LED and Piezo action\n");
                turn_on("RED"); // 빨간색 LED 및 부저 동작
            } else { // 기본 동작
                printf("Green LED action\n");
                turn_on("GREEN"); // 초록색 LED 동작
            }
        }
    }

    // GPIO 핀 해제
    gpio_unexport(RED);       // 빨간색 LED 핀 비활성화
    gpio_unexport(GREEN);     // 초록색 LED 핀 비활성화
    gpio_unexport(BLUE);      // 파란색 LED 핀 비활성화
    gpio_unexport(BUZZER_PIN); // 부저 핀 비활성화

    return NULL; // 스레드 종료
}

// 프로그램의 메인 함수
int main(void) {
    pthread_t gpio_tid, input_tid, connection_tid; // 각 스레드 식별자

    // GPIO 제어 스레드 생성
    if (pthread_create(&gpio_tid, NULL, gpio_thread, NULL) != 0) {
        perror("Failed to create gpio_thread"); // 스레드 생성 실패 시 오류 출력
        return 1;
    }

    // 사용자 입력 스레드 생성
    if (pthread_create(&input_tid, NULL, input_thread, NULL) != 0) {
        perror("Failed to create input_thread"); // 스레드 생성 실패 시 오류 출력
        return 1;
    }

    // UDP 연결 관리 스레드 생성
    if (pthread_create(&connection_tid, NULL, connection_thread, NULL) != 0) {
        perror("Failed to create connection_thread"); // 스레드 생성 실패 시 오류 출력
        return 1;
    }

    // 모든 스레드가 종료될 때까지 대기
    pthread_join(input_tid, NULL); // 사용자 입력 스레드 대기
    pthread_join(gpio_tid, NULL);  // GPIO 제어 스레드 대기
    pthread_join(connection_tid, NULL); // UDP 연결 관리 스레드 대기

    return 0; // 프로그램 종료
}
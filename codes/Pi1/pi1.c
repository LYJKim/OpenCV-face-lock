#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>
#include <netinet/in.h>
#include <sys/socket.h>

// lcd관련 define
#define I2C_ADDR 0x27
#define LCD_CHR 1
#define LCD_CMD 0
#define LCD_BACKLIGHT 0x08
#define ENABLE 0b00000100
#define LCD_LINE_1 0x80
#define LCD_LINE_2 0xC0

// button관련 define
#define IN 0
#define OUT 1

#define PIN 20
#define POUT2 21
#define VALUE_MAX 40
#define DIRECTION_MAX 40

// camera관련 define
#define LOW 0
#define HIGH 1

// server connection관련 define
#define SERVER_ADDRESS_PI2 "192.168.65.8" //pi2 서버 주소
#define SERVER_ADDRESS_PI4 "192.168.65.5" //pi4 서버 주소
#define SERVER_PORT_SEND_PI2 12345 //pi2에게 보낼 떄의 포트번호
#define SERVER_PORT_RECEIVE_PI2 12121 //pi2에게 받을 때의 포트번호
#define SERVER_PORT_PI4 14141 //pi4에게 보낼 때의 포트번호
#define PING_PORT_PI2 54321 //pi2에게 ping을 보내는 포트번호
#define BUFFER_SIZE 1024 //통신할 때 사용하는 버퍼 정의

int ROWS[4] = {19, 13, 6, 5};
int COLS[3] = {25, 24, 23};

char keys[4][3] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'}
};

char sn[10] = {0}; // student number
char pw[5] = {0};  // password
char rn[5] = {0};  // room number
char final[21] = {0}; // pi2에게 보낼 최종적 array

int sn_count = 0;
int pw_count = 0;
int rn_count = 0;

int fd;
atomic_int running = 1;

// GPIO 관련 함수
int GPIOExport(int pin);
int GPIOUnexport(int pin);
int GPIODirection(int pin, int dir);
int GPIORead(int pin);
int GPIOWrite(int pin, int value);
int gpio_read(int pin);
void gpio_export(int pin);
void gpio_unexport(int pin);
void gpio_direction(int pin, int dir);
void gpio_write(int pin, int value);
//LCD 관련 함수
void lcd_init();
void lcd_byte(int bits, int mode);
void lcd_toggle_enable(int bits);
void lcd_string(const char *s, int line);
void lcd_clear();
// 키패드 관련 함수
char read_keypad();
void student_num();
void password();
void room_num();
void final_result();
void input_again(int count);
// 통신 함수
void send_to_pi2(const char *data);
char receive_from_pi2();
void send_to_pi4(const char *zip_file_path);
// 쓰레드 함수
void *exit_thread(void *arg);
void *gpio_thread(void *arg);
void *input_thread(void *arg);
void *ping_thread(void *arg);

int main() {
    pthread_t exit_tid, input_tid, ping_tid, gpio_tid;

    // pi2 핑 처리 쓰레드
    if (pthread_create(&ping_tid, NULL, ping_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create ping thread\n");
        return 1;
    }

    // 실제 입력과 관련된 쓰레드
    if (pthread_create(&input_tid, NULL, input_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create input thread\n");
        return 1;
    }

    // GPIO 처리 담당 쓰레드
    if (pthread_create(&gpio_tid, NULL, gpio_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create GPIO thread\n");
        return 1;
    }
    
    // 종료 처리 담당 쓰레드
    if (pthread_create(&exit_tid, NULL, exit_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create input thread\n");
        return 1;
    }

    // 쓰레드 종료 대기
    pthread_join(input_tid, NULL);
    pthread_join(exit_tid, NULL);
    pthread_join(ping_tid, NULL);
    pthread_join(gpio_tid, NULL);

    return 0;
}

void *input_thread(void *arg) {
    do {
        const char *save_path = "/home/mkms8/image"; //vnc에서 이미지 저장 경로
        char command[256];
        //이미지가 계속 생성되는 것을 방지하기 위해 실행할 때 파일에 있는 이미지 전부 삭제
        snprintf(command, sizeof(command), "rm -rf %s/*", save_path); 
        if (system(command) == 0) {
            printf("All files in %s removed successfully.\n", save_path);
        } else {
            printf("Failed to remove files in %s\n.", save_path);
        }
        char response = 'n';  // 응답을 n으로 정의

        // GPIO 설정
        for (int i = 0; i < 4; i++) {
            gpio_export(ROWS[i]);
            gpio_direction(ROWS[i], 1); // OUTPUT
            gpio_write(ROWS[i], 0);
        }

        for (int i = 0; i < 3; i++) {
            gpio_export(COLS[i]);
            gpio_direction(COLS[i], 0); // INPUT
        }

        // I2C 설정
        char *filename = (char*)"/dev/i2c-1";
        if ((fd = open(filename, O_RDWR)) < 0) {
            fprintf(stderr, "Failed to open the i2c bus\n");
            exit(1);
        }

        if (ioctl(fd, I2C_SLAVE, I2C_ADDR) < 0) {
            fprintf(stderr, "Failed to acquire bus access and/or talk to slave\n");
            exit(1);
        }

        if (GPIOExport(POUT2) == -1 || GPIOExport(PIN) == -1) {
            return NULL;
        }

        // POUT2는 출력, PIN은 입력으로 설정
        if (GPIODirection(POUT2, OUT) == -1 || GPIODirection(PIN, IN) == -1) {
            return NULL;
        }

        do {
            if (GPIOWrite(POUT2, 1) == -1) { 
                return NULL;
            }

            printf("GPIORead: %d from pin %d\n", GPIORead(PIN), PIN);
            lcd_clear();
            lcd_string("No Input", LCD_LINE_1); //입력이 없을 때 lcd에 No Input출력
            usleep(1000000);
            lcd_clear();

            if (GPIORead(PIN) == 1) { //버튼이 눌릴 때
                char file_name[50];

                printf("Pressed Button\n");

                lcd_init(); //No Input을 없애기 위한 초기화

                lcd_string("Enter your", LCD_LINE_1);
                lcd_string("Information", LCD_LINE_2);
                usleep(3000000);
                lcd_clear();

                student_num();
                password();
                room_num();
                lcd_clear();

                final_result();

                // pi2에게 final 배열 전송
                send_to_pi2(final);

                // pi2에게 y혹은 n응답을 받음
                response = receive_from_pi2();

                if (response == 'y') { //pi2의 DB에 중복된 학번이 없다면
                    printf("Response from Pi2: %c\n", response);
                    lcd_string("Take Pictures", LCD_LINE_1);
                    usleep(3000000);
                    lcd_clear();
                    lcd_string("3", LCD_LINE_1);
                    usleep(1000000);
                    lcd_string("2", LCD_LINE_1);
                    usleep(1000000);
                    lcd_string("1", LCD_LINE_1);
                    usleep(1000000);
                    lcd_clear();

                    for (int i = 0; i < 50; i++) { //사진 50장 촬영
                        lcd_string("Taking Pictures", LCD_LINE_1);
                        snprintf(file_name, sizeof(file_name), "%s.%d.jpg", sn, i + 1); // 이미지를 저장할 때의 형식 설정
                        snprintf(command, sizeof(command), "libcamera-jpeg -t 1 --shutter 10000 -n -o %s/%s", save_path, file_name);

                        int result = system(command);

                        if (result == 0) {
                            printf("success: %s/%s\n", save_path, file_name);
                        } else {
                            printf("fail\n");
                        }
                    }

                    snprintf(command, sizeof(command), "zip -j %s/images.zip %s/*.jpg", save_path, save_path); //이미지 zip파일로 압축
                    if (system(command) == 0) {
                        printf("Images compressed successfully.\n");
                        char zip_file_path[256];
                        snprintf(zip_file_path, sizeof(zip_file_path), "%s/images.zip", save_path);
                        send_to_pi4(zip_file_path); //pi4에게 zip파일 전송
                    } else {
                        printf("Failed to compress images.");
                    }

                    lcd_clear();
                    lcd_string("Successed!", LCD_LINE_1);
                    usleep(1000000);

                    // 응답이 y가 아니라면 응답을 n으로 설정
                    response = 'n';

                    break;
                } else if (response == 'n') { //응답이 n이면 다시 입력하라는 문구 lcd에 출력
                    printf("Response from Pi2: %c\n", response);
                    lcd_clear();
                    lcd_string("Try Again!", LCD_LINE_1);
                    usleep(2000000);
                }
            }
        } while (response != 'y');

        if (GPIOUnexport(POUT2) == -1 || GPIOUnexport(PIN) == -1) {
            return NULL;
        }

        close(fd);
    } while (atomic_load(&running));

    for (int i = 0; i < 4; i++) {
        gpio_unexport(ROWS[i]);
    }
    for (int i = 0; i < 3; i++) {
        gpio_unexport(COLS[i]);
    }

    return NULL;
}

void *exit_thread(void *arg) { //exit를 입력하면 프로그램 종료
    char input[100];
    while (atomic_load(&running)) {
        printf("Enter 'exit' to quit: \n");
        if (fgets(input, sizeof(input), stdin) != NULL) {
            input[strcspn(input, "\n")] = 0; 
            if (strcmp(input, "exit") == 0) {
                printf("Exiting program...\n");
                atomic_store(&running, 0);
            }
        }
    }

    return NULL;
}

void *gpio_thread(void *arg) {
    while (atomic_load(&running)) {
        sleep(1);
    }

    for (int i = 0; i < 4; i++) {
        gpio_unexport(ROWS[i]);
    }
    for (int i = 0; i < 3; i++) {
        gpio_unexport(COLS[i]);
    }
    gpio_unexport(PIN);
    gpio_unexport(POUT2);

    if (fd > 0) {
        close(fd);
    }

    exit(0);

    return NULL;
}

// ping을 보내는 쓰레드 함수
void *ping_thread(void *arg) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    char buffer[64];
    char *pong_msg = "PONG";
    socklen_t addr_len = sizeof(struct sockaddr_in);

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PING_PORT_PI2);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Listening for PING requests...\n");

    while (atomic_load(&running)) {
        if (recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &addr_len) > 0) {
            buffer[strlen(buffer)] = '\0';
            printf(buffer);
            printf("Received '%s' from %s\n", buffer, inet_ntoa(client_addr.sin_addr));
            sendto(sockfd, pong_msg, strlen(pong_msg), 0, (struct sockaddr *)&client_addr, addr_len);
            
        }
    }

    close(sockfd);
    return 0;
}

// pi2에게 배열을 보내는 함수
void send_to_pi2(const char *data) { 
    int send_sock;
    struct sockaddr_in server_addr_send;

    send_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (send_sock == -1) {
        perror("Socket creation for sending failed");
        exit(EXIT_FAILURE);
    }

    server_addr_send.sin_family = AF_INET;
    server_addr_send.sin_port = htons(SERVER_PORT_SEND_PI2);
    if (inet_pton(AF_INET, SERVER_ADDRESS_PI2, &server_addr_send.sin_addr) <= 0) {
        perror("Invalid server address for sending");
        close(send_sock);
        exit(EXIT_FAILURE);
    }

    if (connect(send_sock, (struct sockaddr*)&server_addr_send, sizeof(server_addr_send)) == -1) {
        perror("Connection for sending failed");
        close(send_sock);
        exit(EXIT_FAILURE);
    }
    printf("Connected to server %s:%d to send data.\n", SERVER_ADDRESS_PI2, SERVER_PORT_SEND_PI2);

    if (send(send_sock, data, strlen(data), 0) == -1) {
        perror("Failed to send data");
        close(send_sock);
        exit(EXIT_FAILURE);
    }
    printf("Data sent successfully to Pi 2.\n");

    close(send_sock);
}

// pi2로부터 y,n응답을 받는 함수
char receive_from_pi2() {
    int recv_sock;
    struct sockaddr_in server_addr_recv, client_addr;
    char buffer[BUFFER_SIZE];

    recv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (recv_sock == -1) {
        perror("Socket creation for receiving failed");
        exit(EXIT_FAILURE);
    }

    server_addr_recv.sin_family = AF_INET;
    server_addr_recv.sin_port = htons(SERVER_PORT_RECEIVE_PI2);
    server_addr_recv.sin_addr.s_addr = INADDR_ANY;

    if (bind(recv_sock, (struct sockaddr*)&server_addr_recv, sizeof(server_addr_recv)) == -1) {
        perror("Binding receive socket failed");
        close(recv_sock);
        exit(EXIT_FAILURE);
    }

    if (listen(recv_sock, 1) == -1) {
        perror("Listening on receive socket failed");
        close(recv_sock);
        exit(EXIT_FAILURE);
    }

    printf("Listening for response from Pi 2 on port %d...\n", SERVER_PORT_RECEIVE_PI2);

    socklen_t client_len = sizeof(client_addr);
    int client_sock = accept(recv_sock, (struct sockaddr*)&client_addr, &client_len);
    if (client_sock == -1) {
        perror("Accepting connection failed");
        close(recv_sock);
        exit(EXIT_FAILURE);
    }

    ssize_t recv_len = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
    if (recv_len == -1) {
        perror("Failed to receive response from Pi 2");
        close(client_sock);
        close(recv_sock);
        exit(EXIT_FAILURE);
    }
    buffer[recv_len] = '\0';
    printf("Received response from Pi 2: %s\n", buffer);

    close(client_sock);
    close(recv_sock);

    return buffer[0]; //buffer의 첫 번째 문자열 반환
}

void send_to_pi4(const char *zip_file_path) {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT_PI4);
    if (inet_pton(AF_INET, SERVER_ADDRESS_PI4, &server_addr.sin_addr) <= 0) {
        perror("Invalid server address");
        close(sock);
        exit(EXIT_FAILURE);
    }

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Connection failed");
        close(sock);
        exit(EXIT_FAILURE);
    }
    printf("Connected to server %s:%d\n", SERVER_ADDRESS_PI4, SERVER_PORT_PI4);

    FILE *file = fopen(zip_file_path, "rb");
    if (!file) {
        perror("Failed to open zip file");
        close(sock);
        exit(EXIT_FAILURE);
    }

    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        if (send(sock, buffer, bytes_read, 0) == -1) {
            perror("Failed to send file data");
            fclose(file);
            close(sock);
            exit(EXIT_FAILURE);
        }
    }
    printf("File data sent successfully to Pi 4.\n");

    fclose(file);

    usleep(5000000);
    close(sock);
}

int GPIOExport(int pin) {
#define BUFFER_MAX 3
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd == -1) {
        fprintf(stderr, "Failed to open export for writing!\n");
        return -1;
    }

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return 0;
}

int GPIOUnexport(int pin) {
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd == -1) {
        fprintf(stderr, "Failed to open unexport for writing!\n");
        return -1;
    }

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return 0;
}

int GPIODirection(int pin, int dir) {
    static const char s_directions_str[] = "in\0out";

    char path[DIRECTION_MAX];
    int fd;

    snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
    fd = open(path, O_WRONLY);
    if (fd == -1) {
        fprintf(stderr, "Failed to open gpio direction for writing!\n");
        return -1;
    }

    if (write(fd, &s_directions_str[IN == dir ? 0 : 3], IN == dir ? 2 : 3) == -1) {
        fprintf(stderr, "Failed to set direction!\n");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

int GPIORead(int pin) {
    char path[VALUE_MAX];
    char value_str[3];
    int fd;

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "Failed to open gpio value for reading!\n");
        return -1;
    }

    if (read(fd, value_str, 3) == -1) {
        fprintf(stderr, "Failed to read value!\n");
        close(fd);
        return -1;
    }

    close(fd);

    return atoi(value_str);
}

int GPIOWrite(int pin, int value) {
    static const char s_values_str[] = "01";

    char path[VALUE_MAX];
    int fd;

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_WRONLY);
    if (fd == -1) {
        fprintf(stderr, "Failed to open gpio value for writing!\n");
        return -1;
    }

    if (write(fd, &s_values_str[LOW == value ? 0 : 1], 1) != 1) {
        fprintf(stderr, "Failed to write value!\n");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

void gpio_export(int pin) {
    char buffer[3];
    int len = snprintf(buffer, sizeof(buffer), "%d", pin);
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd == -1) {
        fprintf(stderr, "Failed to export GPIO pin %d\n", pin);
        return;
    }
    write(fd, buffer, len);
    close(fd);
}

void gpio_unexport(int pin) {
    char buffer[3];
    int len = snprintf(buffer, sizeof(buffer), "%d", pin);
    int fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd == -1) {
        fprintf(stderr, "Failed to unexport GPIO pin %d\n", pin);
        return;
    }
    write(fd, buffer, len);
    close(fd);
}

void gpio_direction(int pin, int dir) {
    char path[35];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
    int fd = open(path, O_WRONLY);
    if (fd == -1) {
        fprintf(stderr, "Failed to set direction for GPIO pin %d\n", pin);
        return;
    }
    if (dir == 0) {
        write(fd, "in", 2);
    } else {
        write(fd, "out", 3);
    }
    close(fd);
}

void gpio_write(int pin, int value) {
    char path[30];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    int fd = open(path, O_WRONLY);
    if (fd == -1) {
        fprintf(stderr, "Failed to write to GPIO pin %d\n", pin);
        return;
    }
    if (value == 0) {
        write(fd, "0", 1);
    } else {
        write(fd, "1", 1);
    }
    close(fd);
}

int gpio_read(int pin) {
    char path[30];
    char value_str[3];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "Failed to read from GPIO pin %d\n", pin);
        return -1;
    }
    read(fd, value_str, 3);
    close(fd);
    return atoi(value_str);
}

void lcd_init() {
    lcd_byte(0x33, LCD_CMD);
    lcd_byte(0x32, LCD_CMD);
    lcd_byte(0x06, LCD_CMD);
    lcd_byte(0x0C, LCD_CMD);
    lcd_byte(0x28, LCD_CMD);
    lcd_byte(0x01, LCD_CMD);
    usleep(5000);
}

void lcd_byte(int bits, int mode) {
    int bits_high = mode | (bits & 0xF0) | LCD_BACKLIGHT;
    int bits_low = mode | ((bits << 4) & 0xF0) | LCD_BACKLIGHT;

    write(fd, &bits_high, 1);
    lcd_toggle_enable(bits_high);

    write(fd, &bits_low, 1);
    lcd_toggle_enable(bits_low);
}

void lcd_toggle_enable(int bits) {
    usleep(500);
    bits |= ENABLE;
    write(fd, &bits, 1);
    usleep(500);
    bits &= ~ENABLE;
    write(fd, &bits, 1);
    usleep(500);
}

void lcd_string(const char *s, int line) {
    lcd_byte(line, LCD_CMD);
    while (*s) {
        lcd_byte(*(s++), LCD_CHR);
    }
}

void lcd_clear() {
    lcd_byte(0x01, LCD_CMD);
}

//키패드 입력을 읽어오는 함수
char read_keypad() { 
    for (int row = 0; row < 4; row++) {
        gpio_write(ROWS[row], 1);
        for (int col = 0; col < 3; col++) {
            if (gpio_read(COLS[col]) == 1) {
                char pressed_key = keys[row][col];
                usleep(300000); //usleep이 없으면 키패드가 혼자 눌림
                gpio_write(ROWS[row], 0);
                return pressed_key;
            }
        }
        gpio_write(ROWS[row], 0);
    }
    return '\0';
}

//학번을 받는 함수
void student_num() { 
    lcd_string("Student ID", LCD_LINE_1);
    while (1) {
        char pressed_key = read_keypad();
        if (pressed_key) {
            if (pressed_key == '#') {
                if (sn_count < 9) { //학번 9자리를 입력하지 않은 경우
                    lcd_clear();
                    lcd_string("Enter Full ID", LCD_LINE_1);
                    usleep(2000000);
                } else {
                    sn[sn_count] = '\0';
                    printf("Final Student ID: %s\n", sn);
                    usleep(300000);
                    lcd_clear();
                    return;
                }
            } else if (pressed_key == '*') {
                if (sn_count > 0) {
                    sn_count--;
                    sn[sn_count] = '\0';
                }
            } else {
                if (sn_count < 9) {
                    sn[sn_count++] = pressed_key;
                }
            }
            lcd_clear();
            lcd_string("Student ID", LCD_LINE_1);
            lcd_string(sn, LCD_LINE_2);
        }
    }
}

// 비밀번호를 입력받는 함수
void password() {
    lcd_string("Password", LCD_LINE_1);
    while (1) {
        char pressed_key = read_keypad();
        if (pressed_key) {
            if (pressed_key == '#') {
                if (pw_count < 4) { //비밀번호 4자리를 입력하지 않은 경우
                    lcd_clear();
                    lcd_string("Enter Full", LCD_LINE_1);
                    lcd_string("PassWord", LCD_LINE_2);
                    usleep(2000000);
                } else {
                    pw[pw_count] = '\0';
                    printf("Final Password: %s\n", pw);
                    usleep(300000);
                    lcd_clear();
                    return;
                } 
            } else if (pressed_key == '*') {
                if (pw_count > 0) {
                    pw_count--;
                    pw[pw_count] = '\0';
                }
            } else {
                if (pw_count < 4) {
                    pw[pw_count++] = pressed_key;
                }
            }
            lcd_clear();
            lcd_string("Password", LCD_LINE_1);
            lcd_string(pw, LCD_LINE_2);
        }
    }
}

// 방호수를 입력받는 함수
void room_num() {
    lcd_string("Room Number", LCD_LINE_1);
    while (1) {
        char pressed_key = read_keypad();
        if (pressed_key) {
            if (pressed_key == '#') {
                if (rn_count < 4) {
                    lcd_clear();
                    lcd_string("Enter Full", LCD_LINE_1);
                    lcd_string("Room Number", LCD_LINE_2);
                    usleep(2000000);
                } else {
                    rn[rn_count] = '\0';
                    printf("Final Room Number: %s\n", rn);
                    usleep(300000);
                    lcd_clear();
                    return;
                }
            } else if (pressed_key == '*') {
                if (rn_count > 0) {
                    rn_count--;
                    rn[rn_count] = '\0';
                }
            } else {
                if (rn_count < 4) {
                    rn[rn_count++] = pressed_key;
                }
            }
            lcd_clear();
            lcd_string("Room Number", LCD_LINE_1);
            lcd_string(rn, LCD_LINE_2);
        }
    }
}

//pi2에게 보내는 배열을 r000000000#0000#0000형태로 설정
void final_result() {
    int index = 0;

    final[index++] = 'r';
 
    for (int i = 0; i < sn_count; i++) {
        final[index++] = sn[i];
    }

    final[index++] = '#';

    for (int i = 0; i < pw_count; i++) {
        final[index++] = pw[i];
    }

    final[index++] = '#';

    for (int i = 0; i < rn_count; i++) {
        final[index++] = rn[i];
    }

    final[index] = '\0';

    printf("Final Result: %s\n", final);
}

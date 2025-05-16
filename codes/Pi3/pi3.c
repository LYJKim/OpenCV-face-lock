#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>

// LCD 관련 설정 
#define I2C_ADDR 0x27 // LCD 시작 주소
#define LCD_CHR 1 // 문자 데이터 전송 모드
#define LCD_CMD 0 // 명령 데이터 전송 모드 
#define LCD_BACKLIGHT 0x08 // LCD 백라이트 설정
#define ENABLE 0b00000100 // LCD Enable
#define LCD_LINE_1 0x80 // LCD 첫번째 라인 주소
#define LCD_LINE_2 0xC0 // LCD 두번째 라인 주소

// GPIO 핀 방향, 값 정의
#define IN 0 
#define OUT 1
#define LOW 0
#define HIGH 1

// 모터 관련 설정
//#define VALUE_MAX 40
//#define DIRECTION_MAX 40
#define PWM 0 // PWM 활성을 위한 기본 값
#define VALUE_MAX_moter 256 // 모터 값 설정 최대 크기
#define DIRECTION_MAX_moter 256 // 모터의 방향 설정 최대 크기

// 소켓 설정
#define SERVER_ADDRESS1 "192.168.65.8" // 파이 2의 주소
#define SERVER_PORT1 23232  // 파이 2로부터 비밀번호 받는 소켓의 포트
#define SERVER_PORT_SEND 12345 // 외부인 감지 시 파이 2에게 알림 보낼 포트

#define SERVER_PING "192.168.65.3" // 파이 3 주소 (자기자신)
#define SERVER_PORT_PING 54321 // 파이 2에게 PING 받을 포트                

#define SERVER_ADDRESS2 "192.168.65.5" // 파이 4 주소
#define SERVER_PORT2 34343  // 파이 4에게 Y 메세지 보낼 포트

#define BUFFER_SIZE 1024 // 버퍼 크기 정의

int ROWS[] = {19, 13, 6, 5}; // 키패드 행 핀 번호
int COLS[] = {25, 24, 23}; // 키패드 열 핀 번호
char keys[4][3] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'}}; // 키패드 번호 정의
int count_idx = 0; // 사용자 비밀 번호 입력 인덱스
int i2c_fd; // I2C 파일 디스크립터

int server_fd, new_socket; // 서버 소켓 정의
struct sockaddr_in address; // 서버 주소 구조체 
int addrlen = sizeof(address); // 주소 길이 정의
char pw_buffer[BUFFER_SIZE] = {0}; // 파이 2로부터 받는 비밀번호 저장
int sock; // 소켓 디스크립터
struct sockaddr_in server_addr; // 서버 주고 구조체
char messages[10] = {0,}; // 서버로 보낼 메세지 저장
char pw[5]; // 사용자가 입력한 비밀번호 저장
char display_pw[20] = "Password: "; // LCD에 출력할 내용
atomic_int running = 1; // 프로그램 실행 값

// LCD 관련 함수
void lcd_init();
void lcd_byte(int bits, int mode);
void lcd_toggle_enable(int bits);
void lcd_string(const char *s, int line);
void lcd_update_password();
void lcd_clear();

// GPIO 관련 함수
static int GPIOExport(int pin);
static int GPIOUnexport(int pin);
static int GPIODirection(int pin, int dir);
static int GPIORead(int pin);
static int GPIOWrite(int pin, int value);

// 키패드 관련 함수
int read_keypad();

// 모터 관련 함수
static int PWMExport(int pwmnum);
static int PWMEnable(int pwmnum);
static int PWMWritePeriod(int pwmnum, int value);
static int PWMWriteDutyCycle(int pwmnum, int value);

// 그 외 함수
int file_exists(const char *path);
int send_sock(char *SERVER_ADDRESS, int SERVER_PORT, char *messages);
void *check_main(void *arg);
void *exit_main(void *arg);
void *thread_main(void *arg);

// GPIO 핀 내보냄
static int GPIOExport(int pin)
{
    char buffer[3];
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd == -1)
        return -1;
    snprintf(buffer, sizeof(buffer), "%d", pin);
    write(fd, buffer, strlen(buffer));
    close(fd);
    return 0;
}

// GPIO 핀 해제
static int GPIOUnexport(int pin)
{
    char buffer[3];
    int fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd == -1)
        return -1;
    snprintf(buffer, sizeof(buffer), "%d", pin);
    write(fd, buffer, strlen(buffer));
    close(fd);
    return 0;
}

// GPIO 핀 방향 설정
static int GPIODirection(int pin, int dir)
{
    char path[40];
    int fd;
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
    fd = open(path, O_WRONLY);
    if (fd == -1)
        return -1;
    if (dir == IN)
        write(fd, "in", 2);
    else
        write(fd, "out", 3);
    close(fd);
    return 0;
}

// GPIO 핀 값 읽음
static int GPIORead(int pin)
{
    char path[40], value[3];
    int fd;
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_RDONLY);
    if (fd == -1)
        return -1;
    read(fd, value, 3);
    close(fd);
    return atoi(value);
}

// GPIO 핀에 값 씀
static int GPIOWrite(int pin, int value)
{
    char path[40];
    int fd;
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_WRONLY);
    if (fd == -1)
        return -1;
    if (value == LOW)
        write(fd, "0", 1);
    else
        write(fd, "1", 1);
    close(fd);
    return 0;
}

// LCD 초기화
void lcd_init()
{
    if ((i2c_fd = open("/dev/i2c-1", O_RDWR)) < 0)
    {
        perror("Failed to open I2C device");
        exit(1);
    }

    if (ioctl(i2c_fd, I2C_SLAVE, I2C_ADDR) < 0)
    {
        perror("Failed to acquire bus access");
        exit(1);
    }

    lcd_byte(0x33, LCD_CMD);
    lcd_byte(0x32, LCD_CMD);
    lcd_byte(0x06, LCD_CMD);
    lcd_byte(0x0C, LCD_CMD);
    lcd_byte(0x28, LCD_CMD);
    lcd_byte(0x01, LCD_CMD);
    usleep(5000);
}

// LCD 데이터 전송
void lcd_byte(int bits, int mode)
{
    int bits_high = mode | (bits & 0xF0) | LCD_BACKLIGHT;
    int bits_low = mode | ((bits << 4) & 0xF0) | LCD_BACKLIGHT;

    write(i2c_fd, &bits_high, 1);
    lcd_toggle_enable(bits_high);

    write(i2c_fd, &bits_low, 1);
    lcd_toggle_enable(bits_low);
}

// LCD Enable 신호 설정
void lcd_toggle_enable(int bits)
{
    usleep(500);
    int enable = bits | ENABLE;
    write(i2c_fd, &enable, 1);
    usleep(500);
    int disable = bits & ~ENABLE;
    write(i2c_fd, &disable, 1);
    usleep(500);
}

// LCD 문자열 출력
void lcd_string(const char *s, int line)
{
    lcd_byte(line, LCD_CMD);
    while (*s)
        lcd_byte(*(s++), LCD_CHR);
}

// LCD에 비밀 번호 입력 시 화면 업데이트
void lcd_update_password()
{
    for (int i = 0; i < count_idx; i++)
    {
        display_pw[10 + i] = '*';
    }
    display_pw[10 + count_idx] = '\0';
    lcd_string(display_pw, LCD_LINE_2);
}

// LCD 화면 clear
void lcd_clear(void)
{
    lcd_byte(0x01, LCD_CMD);
}

// 키패드 값 읽어와 사용자가 입력한 비밀번호 저장
int read_keypad()
{
    for (int row_idx = 0; row_idx < 4; row_idx++) 
    {
        GPIOWrite(ROWS[row_idx], 1); // 현재 행 활성화
        for (int col_idx = 0; col_idx < 3; col_idx++)
        {
            if (GPIORead(COLS[col_idx])) // 현재 열의 버튼이 눌러졌는지 확인
            {
                if (keys[row_idx][col_idx] == '#') // # 버튼을 누르면 비밀 번호 입력 종료
                {
                    return 0;
                }
                pw[count_idx] = keys[row_idx][col_idx]; // 입력된 값 비밀번호 배열에 저장
                count_idx++; // 비밀번호 저장 인덱스 증가
                lcd_update_password(); // 하나 입력할 때마다 LCD에 * 출력
                usleep(100000);
            }
        }
        GPIOWrite(ROWS[row_idx], 0); // 현재 행 비활성화
    }
    return 1; // # 누르지 않아 비밀 번호 계속 입력 하도록 유지
}

// PWM 활성화 함수
static int PWMExport(int pwmnum) 
{
    #define BUFFER_MAX 3
    char buffer[BUFFER_MAX];
    int fd, byte;

    fd = open("/sys/class/pwm/pwmchip0/export", O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open export for export!\n");
        return (-1);
    }

    byte = snprintf(buffer, BUFFER_MAX, "%d", pwmnum);
    write(fd, buffer, byte);
    close(fd);
    sleep(1);
    return (0);
}

// PWM 채널 활성화 함수
static int PWMEnable(int pwmnum)
{
    static const char s_enable_str[] = "1";
    char path[DIRECTION_MAX_moter];
    int fd;

    snprintf(path, DIRECTION_MAX_moter, "/sys/class/pwm/pwmchip0/pwm0/enable", pwmnum);
    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open in enable!\n");
        return -1;
    }

    write(fd, s_enable_str, strlen(s_enable_str));
    close(fd);

    return (0);
}

// PWM 주기 설정 함수
static int PWMWritePeriod(int pwmnum, int value)
{
    char s_value_str[VALUE_MAX_moter];
    char path[VALUE_MAX_moter];
    int fd, byte;

    snprintf(path, VALUE_MAX_moter, "/sys/class/pwm/pwmchip0/pwm0/period", pwmnum);
    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open in period!\n");
        return (-1);
    }
    byte = snprintf(s_value_str, VALUE_MAX_moter, "%d", value);

    if (-1 == write(fd, s_value_str, byte))
    {
        fprintf(stderr, "Failed to write value in period!\n");
        close(fd);
        return -1;
    }
    close(fd);

    return (0);
}

// PWM 듀티 사이클을 설정 함수
static int PWMWriteDutyCycle(int pwmnum, int value)
{
    char s_value_str[VALUE_MAX_moter];
    char path[VALUE_MAX_moter];
    int fd, byte;

    snprintf(path, VALUE_MAX_moter, "/sys/class/pwm/pwmchip0/pwm0/duty_cycle", pwmnum);
    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open in duty cycle!\n");
        return (-1);
    }
    byte = snprintf(s_value_str, VALUE_MAX_moter, "%d", value);
    if (-1 == write(fd, s_value_str, byte))
    {
        fprintf(stderr, "Failed to write value in duty cycle!\n");
        close(fd);
        return -1;
    }
    close(fd);
    return (0);
}

// 파일 존재 여부 확인 함수
int file_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

// 소켓으로 메세지 전달 함수
int send_sock(char *SERVER_ADDRESS, int SERVER_PORT, char *messages)
{
    char *message = malloc(10); // 서버에게 보낼 메세지
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) // 소켓 생성
    {
        perror("Socket creation failed");
        return EXIT_FAILURE;
    }
    server_addr.sin_port = htons(SERVER_PORT); // 서버 포트 설정
    server_addr.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;

    if (inet_pton(AF_INET, SERVER_ADDRESS, &server_addr.sin_addr) <= 0) // 서버 주소 설정
    {
        perror("Invalid server address");
        close(sock);
        return EXIT_FAILURE;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) // 서버 연결
    {
        perror("Connection failed");
        close(sock);
        return EXIT_FAILURE;
    }
    printf("Connected to server %s:%d\n", SERVER_ADDRESS, SERVER_PORT);

    strcpy(message, messages); // 매개변수로 받은 메세지를 보낼 메세지로 복사
    printf("message : %s\n",message);
    int n;
    if ((n = send(sock, message, strlen(message), 0)) == -1) // 소켓 전송
    {
        perror("Send failed");
        printf("%d\n",n);
        close(sock);
        return EXIT_FAILURE;
    }
    printf("Sent to server: %s\n", message);
    close(sock);
}

// 파이 2로 PING 메세지 받고 PONG 메세지 보내는 함수
void *check_main(void *arg)
{
    int sockfd; // 소켓 파일 디스크립터
    struct sockaddr_in server_addr, client_addr; // 서버와 클라이언트 주소 구조체
    char buffer[64]; // 받은 데이터 저장할 버퍼
    char *pong_msg = "PONG"; // 소켓으로 보낼 메시지
    socklen_t addr_len = sizeof(struct sockaddr_in); 

    // 소켓 생성
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // 서버 주소 구조체 초기화
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT_PING);

    // 소켓 바인딩
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("Listening for PING requests...\n");
    
    // PING 메세지를 받기까지 무한 대기
    while (1) {
        if (recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &addr_len) > 0) {
            buffer[strlen(buffer)] = '\0'; 
            printf(buffer);
            printf("Received '%s' from %s\n", buffer, inet_ntoa(client_addr.sin_addr));
            // 답신으로 PONG 메세지 전송
            sendto(sockfd, pong_msg, strlen(pong_msg), 0, (struct sockaddr *)&client_addr, addr_len);
            
        }
    }

    close(sockfd);
    return 0;
}

// exit 입력 시 프로그램 종료되도록 하는 함수
void *exit_main(void *arg)
{
    char cmd[40]; // 종료시 실행할 명령어 저장용
    char input[100]; // 사용자 입력
    const char *filepath="./cmd"; // 종료 시 실행할 외부 프로그램 
    // - PING 메세지를 무한정 대기하는 함수에서 탈출하기 위해 나에게 PING 메세지를 보내는 외부 프로그램 실행
    while (atomic_load(&running)) // 프로그램이 실행 중인 동안 반복
    {
        printf("Enter exit to quit: ");
        if(fgets(input, sizeof(input), stdin) != NULL) // 사용자 입력
        {
            input[strcspn(input, "\n")] = 0;
            if(strcmp(input, "exit") == 0) // 입력이 exit이라면 종료
            {
                printf("Exiting program...\n");
                atomic_store(&running, 0);
                snprintf(cmd, sizeof(cmd), "%s",filepath);
                system(cmd); // 외부 프로그램 실행
            }
        }
    }
}

// 센서의 전체 로직 수행 함수
void *thread_main(void *arg)
{
    char *messages = malloc(10); // 소켓으로 보낼 메세지
    strcpy(messages, "Y\n"); // 파이 4에게 모션 감지 on 메세지 복사
    send_sock(SERVER_ADDRESS2, SERVER_PORT2, messages); // 파이 4에게 모션 감지 on 메세지 전송
    
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) // 소켓 생성
    {
        perror("Socket Create Failure");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET; // 서버 주소 설정
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(SERVER_PORT1);

    // 소켓 바인딩
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("Binding Failure");
        exit(EXIT_FAILURE);
    }

    // 클라이언트 연결 대기
    if (listen(server_fd, 3) < 0)
    {
        perror("Connection Waiting Failure");
        exit(EXIT_FAILURE);
    }
    
    // 프로그램 실행 상태일 때만 로직 진행
    while (atomic_load(&running))
    {   
        // GPIO 핀 초기화
        for (int i = 0; i < 4; i++)
        {
            GPIOExport(ROWS[i]);
            GPIODirection(ROWS[i], OUT);
            GPIOWrite(ROWS[i], LOW);
        }
        for (int i = 0; i < 3; i++)
        {
            GPIOExport(COLS[i]);
            GPIODirection(COLS[i], IN);
        }
        // 클라이언트 연결 수락
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
        {
            perror("Client Connection Failure");
            exit(EXIT_FAILURE);
        }
        printf("Client connected.\n");

        // 파이 2가 보낸 데이터 베이스에 저장된 비밀 번호 읽어 비밀번호 버퍼에 저장
        int valread = read(new_socket, pw_buffer, BUFFER_SIZE);
        printf("read : %d\n", valread);

        // 버퍼로 읽어온 값이 있을 때 모든 로직 수행 가능, 즉 파이 2로부터 비밀 번호 받아온 뒤 센서들 실행됨
        if (valread > 0)
        {
            if(!strcmp(pw_buffer, "exit")) //? 이게 뭐야
            {
                break;
            } 
            pw_buffer[valread-1] = '\0';
            printf("Message from Java: %s\n", pw_buffer);
            lcd_init(); // LCD 초기화

            // 총 5번까지 비밀번호 입력 가능하므로 5번 반복
            for (int i = 0; i < 5; i++)
            {
                lcd_clear();
                lcd_string("Enter Password", LCD_LINE_1); // 비밀 번호 입력 메세지 출력
                lcd_string("Password: ", LCD_LINE_2);
                memset(pw, 0, sizeof(pw)); // 비밀번호 새로 입력할 때마다 비밀번호 입력 버퍼 초기화
                int n;
                while (1)
                {
                    n = read_keypad(); // 사용자가 입력한 비밀 번호 저장
                    if (n == 0)
                    {
                        break; // 사용자가 # 눌렀을 때만 키패드 입력 종료
                    }
                }
                printf("pw : %s\n", pw);
                if (!strcmp(pw_buffer, pw)) // 사용자의 비밀번호와 파이 2로 받은 비밀 번호가 일치할 때
                {
                    lcd_clear();
                    lcd_string("correct", LCD_LINE_1);
                    char path[128];
                    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip0/pwm%d", PWM); // 모터 제어를 위해 PWM 설정
                    if (!file_exists(path))
                    {
                        snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip0/export");
                        int fd = open(path, O_WRONLY);
                        if (fd < 0)
                        {
                            perror("Failed to export PWM channel");
                        }
                        write(fd, "0", 1);
                        close(fd);
                        sleep(1);
                    }
                    PWMExport(PWM);
                    PWMWritePeriod(PWM, 20000000);
                    PWMWriteDutyCycle(PWM, 1800000); // 모터 이동
                    sleep(1);
                    PWMWriteDutyCycle(PWM, 700000); // 모터 이동
                    PWMEnable(PWM);
                    PWMWriteDutyCycle(PWM, 1800000);
                    sleep(3);
                    PWMWriteDutyCycle(PWM, 700000);
                    sleep(3);
                    // PWM 종료
                    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip0/pwm%d/enable", PWM); 
                    int fd = open(path, O_WRONLY);
                    if (fd < 0)
                    {
                        perror("Failed to disable PWM channel");
                    }
                    write(fd, "0", 1);
                    close(fd);
                    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip0/unexport");
                    fd = open(path, O_WRONLY);
                    if (fd < 0)
                    {
                        perror("Failed to unexport PWM channel");
                    }
                    write(fd, "0", 1);
                    close(fd);
                    strcpy(messages, "Y\n");
                    send_sock(SERVER_ADDRESS2, SERVER_PORT2, messages); // 새로운 사람 다시 감지할 수 있도록 하기 위해 파이 4에게 모션감지 on 메세지 전송
                    // PIR 모션 센서는 한 사람이 키패드 입력 중일 때는 다른 사람을 인식 하면 안되므로 키패드 로직이 다 끝나면 다시 PIR 감지하도록 함
                    break;
                }
                // 비밀 번호가 일치하지 않는 경우
                if (i < 2) // 두번까지는 틀려도 wait 없음
                {
                    lcd_clear();
                    lcd_string("Password wrong", LCD_LINE_1);
                    sleep(1);
                    memset(pw, 0, sizeof(pw)); // 재입력을 위해 버퍼 초기화
                    count_idx = 0; // 재입력을 위해 인덱스 초기화
                    continue; // 다시 입력 받음
                }
                else if (i == 2 || i == 3) // 비밀번호3, 4번 틀렸을 때
                {
                    lcd_clear(); // wait 메세지 출력을 위해 LCD clear
                    lcd_string("Please wait", LCD_LINE_1);
                    lcd_string("30 sec", LCD_LINE_2); // 틀리면 30초 대기
                    lcd_clear();
                    for (int i = 30; i > 0; i--) // 30초 카운트다운 LCD 출력
                    {
                        char count_buffer[15];
                        lcd_clear();
                        sprintf(count_buffer, "%d sec", i);
                        lcd_string("Please wait", LCD_LINE_1);
                        lcd_string(count_buffer, LCD_LINE_2);
                        sleep(1);
                    }
                    memset(pw, 0, sizeof(pw)); // 30초 카운트다운 끝난 후 재입력을 위해 버퍼 초기화
                    count_idx = 0; // 재입력을 위해 인덱스 초기화

                    continue;
                }
                // 마지막 5번 틀렸을 때, 외부인으로 감지
                else
                {
                    strcpy(messages, "wFailed\n"); 
                    send_sock(SERVER_ADDRESS1, SERVER_PORT_SEND, messages); // 외부인으로 감지 후 파이 2에게 알림 전송
                    strcpy(messages, "Y\n");
                    send_sock(SERVER_ADDRESS2, SERVER_PORT2, messages); // 외부인으로 감지 후 새로운 사람을 인식하기 위해 파이 4에게 모션 감지 on 메세지 전송
                }
            }
            lcd_clear();
        }
    }
    // GPIO 핀 해제
    for (int i = 0; i < 4; i++) GPIOUnexport(ROWS[i]);
    for (int i = 0; i < 3; i++) GPIOUnexport(COLS[i]);

    close(i2c_fd); // I2C 디바이스 닫기
    close(new_socket); // 소켓 닫기
    close(server_fd); // 서버 소켓 닫기
    
    return NULL;
}

// 스레드화한 메인 함수
int main()
{
    pthread_t main_thread, check_thread, exit_thread;
    
    pthread_create(&exit_thread, NULL, exit_main, NULL); // 종료 처리 스레드 생성
    pthread_create(&main_thread, NULL, thread_main, NULL); // 센서의 전체 로직 수행하는 스레드 생성
    pthread_create(&check_thread, NULL, check_main, NULL); // PING 메세지 처리 스레드 생성
    
    pthread_join(exit_thread, NULL); // 종료 처리 스레드기 완료될 때까지 대기
    pthread_join(main_thread, NULL); // 센서의 전체 로직 수행하는 스레드가 완료될 때까지 대기
    pthread_join(check_thread, NULL); // PING 메세지 처리 스레드가 완료될 때까지 대기
    
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/socket.h>


#define SERVER_ADDRESS "192.168.65.8" // pi2의 주소
#define MAIN_SERVER_PORT 12345 // pi2와 통신하는 port (카메라에 인식된 사람이면 등록 유무에 따라 학번 혹은 munoknown 으로 보내기)
#define LISTEN_PORT 34343 // pi3와 모션 감지 해도 된다는 사인을 통신하는 port
#define BUFFER_SIZE 1024
#define ERROR_PORT 54321 // pi2와 ping pong을 주고 받는 port

#define SEND_FILE_PATH "/home/ruby/Desktop/recognize_student.txt" // 결과 저장될 txt 파일 주소
#define TMP_FILE_PATH "/home/ruby/Desktop/tmp_recognize_student.txt"

#define IN 0
#define OUT 1

#define LOW 0
#define HIGH 1

#define PIR_PIN 539 // GPIO 27에 해당하는 것인데 라즈베리파이에서 27번을 539번으로 인식함.
#define VALUE_MAX 40
#define DIRECTION_MAX 40

atomic_int running =1;

void pir_to_capture(int i);
// with pi2
int communicate_with_server(const char *server_address, int port, const char *file_path) {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    FILE *file;
    size_t bytes_read;
    // 소켓 만들기
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Socket creation failed");
        return -1;
    }
    // server address 만들기
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_address, &server_addr.sin_addr) <= 0) {
        perror("Invalid server address");
        close(sock);
        return -1;
    }
    // 연결
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Connection failed");
        close(sock);
        return -1;
    }
    printf("Connected to server %s:%d\n", server_address, port);
    // 파일 열기
    file = fopen(file_path, "rb");
    if (file == NULL) {
        perror("File open failed");
        close(sock);
        return -1;
    }
    // 파일 전송
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        if (send(sock, buffer, bytes_read, 0) == -1) {
            perror("File send failed");
            fclose(file);
            close(sock);
            return -1;
        }
    }
    // 파일 전송 완료
    printf("File '%s' sent successfully.\n", file_path);
    fclose(file);
    close(sock);
    return 0;
}


//with pi3

void receive_message_on_port(int port) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char* buffer = malloc(1024);
    // server socket 만들기
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("Socket Create Failure");
        exit(EXIT_FAILURE);
    }
    // server address 만들기
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port); // port 번호 34343 
    // 연결
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Binding Failure");
        exit(EXIT_FAILURE);
    }
    // listening
    if (listen(server_fd, 3) < 0) {
        perror("Connection Waiting Failure");
        exit(EXIT_FAILURE);
    }
    printf("Server is waiting at port %d...\n", LISTEN_PORT);
    // client 연결
    while (atomic_load(&running)) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (new_socket < 0) {
            perror("Client Connection failure");
            continue;
        }
        printf("Client connected.\n");
        // buffer를 만들기
        int valread = read(new_socket, buffer, BUFFER_SIZE);
        if (valread > 0) {
            buffer[valread] = '\0'; 
            printf("Message is: %s\n", buffer); 
            if(strcmp(buffer,"Y\n")==0 || strcmp(buffer, NULL)==0){
                int i=1;
                pir_to_capture(i);
                //free(buffer);
            }
        }
        else{
            perror("Read failed");    
            buffer=NULL;
        }
        close(new_socket);
    }
    close(server_fd);
}

/*
process to pir
gpio 관련 함수들
*/
static int GPIOExport(int pin) {
  char buffer[4];
  ssize_t bytes_written;
  int fd;

  fd = open("/sys/class/gpio/export", O_WRONLY);
  if (-1 == fd) {
    fprintf(stderr, "Failed to open export for writing!\n");
    return (-1);
  }

  bytes_written = snprintf(buffer, sizeof(buffer), "%d", pin);
  write(fd, buffer, bytes_written);
  close(fd);
  return (0);
}

static int GPIOUnexport(int pin) {
  char buffer[4];
  ssize_t bytes_written;
  int fd;

  fd = open("/sys/class/gpio/unexport", O_WRONLY);
  if (-1 == fd) {
    fprintf(stderr, "Failed to open unexport for writing!\n");
    return (-1);
  }

  bytes_written = snprintf(buffer, sizeof(buffer), "%d", pin);
  write(fd, buffer, bytes_written);
  close(fd);
  return (0);
}

static int GPIODirection(int pin, int dir) {
  static const char s_directions_str[] = "in\0out";

  char path[DIRECTION_MAX];
  int fd;

  snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
  fd = open(path, O_WRONLY);
  if (-1 == fd) {
    fprintf(stderr, "Failed to open gpio direction for writing!\n");
    return (-1);
  }

  if (-1 == write(fd, &s_directions_str[IN == dir ? 0 : 3], IN == dir ? 2 : 3)) {
    fprintf(stderr, "Failed to set direction!\n");
    return (-1);
  }

  close(fd);
  return (0);
}

static int GPIORead(int pin) {
  char path[VALUE_MAX];
  char value_str[3];
  int fd;

  snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
  fd = open(path, O_RDONLY);
  if (-1 == fd) {
    fprintf(stderr, "Failed to open gpio value for reading!\n");
    return (-1);
  }

  if (-1 == read(fd, value_str, sizeof(value_str))) {
    fprintf(stderr, "Failed to read value!\n");
    return (-1);
  }

  close(fd);
  return atoi(value_str);
}

// 카메라를 통해서 사진 찍기
void pir_to_capture(int i) { 
    char command[100];
    printf("pir_to_capture is ready\n");
    if (GPIOExport(PIR_PIN) == -1) {
        snprintf(command, sizeof(command),"export faile");
        return;
    }
    if (GPIODirection(PIR_PIN, IN) == -1) {
        snprintf(command, sizeof(command), "echo out > /sys/class/gpio/gpio539/direction");
        system(command);        
    }
    // 모션 감지
     while(i && atomic_load(&running)){
        if(GPIORead(PIR_PIN) == LOW){
            i=1;
            printf("%d",atomic_load(&running));
            printf("No motion detected1111.\n");
            sleep(5);
            }
        else{
            printf("pir_to_capture is ready\n");
            if (GPIORead(PIR_PIN) == HIGH) {
                printf("Motion detected! Capturing photo...\n");
                // 사진 저장 경로 및 이름 설정
                snprintf(command,sizeof(command),"libcamera-jpeg -o /home/ruby/Desktop/image/predict.jpg");
                // 사진 촬영 명령 실행 python 파일 명령 실행
                system(command);
                const char *python_command = "/usr/bin/python3.9 /home/ruby/Desktop/raspi_predict.py";
                setenv("PYTHONPATH", "/home/ruby/.local/lib/python3.9/site-packages", 1);
                if (system(python_command) != 0) {
                    fprintf(stderr, "Failed to execute Python script: %s\n", python_command);
                    return;
                }
                
                FILE *file = fopen(SEND_FILE_PATH, "r");
                if(file == NULL){
                    perror("Failed to open file");
                    continue;
                }
                // 파일에서 사람 정보 찍기
                char file_content[BUFFER_SIZE];
                if(fgets(file_content,sizeof(file_content), file) != NULL){
                    printf("Read from file: %s\n",file_content);
                    if(!strcmp(file_content, "notpeople\n")){ // 사람이 안 찍혔을 때 처리
                        i=1;
                        continue;
                    }
                    if(!strcmp(file_content, "munknown\n")){ // 등록되지 않은 사람일때의 처리
                        i=1;
                        continue;
                    }
                }
                else{
                    perror("Failed to read file content"); 
                }
                fclose(file);
                
                if (communicate_with_server(SERVER_ADDRESS, MAIN_SERVER_PORT, SEND_FILE_PATH) != 0) {
                    fprintf(stderr, "Failed to communicate with the server\n");
                    return;
                    i=0;
                }
                i=0;
            }
        }       
    }
}

// gpio thread
void *gpio_thread(void* arg){
    while(atomic_load(&running)){
        receive_message_on_port(LISTEN_PORT);
    }
    return NULL;
}
// input thread
void *input_thread(void *arg) {
    char input[100];
    char cmd[40];
    const char *filepath = "./rec_me";
    while (atomic_load(&running)) {
        printf("Enter 'exit' to quit: ");
        if (fgets(input, sizeof(input), stdin) != NULL) {
            input[strcspn(input, "\n")] = 0; 
            if (strcmp(input, "exit") == 0) {
                printf("Exiting program...\n");
                atomic_store(&running, 0);
                snprintf(cmd, sizeof(cmd), "%s", filepath);
                system(cmd);
            }
        }
    }

    return NULL;
}
// ping 주고 받는 thread
void *ping_thread(void* arg){
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
    server_addr.sin_port = htons(ERROR_PORT);

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

int main(int argc, char*argv[]) {
   pthread_t gpio_thread_id, input_thread_id, ping_thread_id;
   printf("Starting program\n");
   
   if (pthread_create(&gpio_thread_id, NULL, gpio_thread, NULL) != 0) {
        perror("pthread_create for gpio_thread failed");
        return 1;
  }

  if (pthread_create(&input_thread_id, NULL, input_thread, NULL) != 0) {
        perror("pthread_create for input_thread failed");
        atomic_store(&running, 0);
        pthread_join(gpio_thread_id, NULL);
        return 1;
  }
  if(pthread_create(&ping_thread_id, NULL, ping_thread, NULL) != 0) {
        perror("pthread_create for ping_thread failed");
        atomic_store(&running, 0);
        pthread_join(gpio_thread_id, NULL);
        pthread_join(input_thread_id, NULL);
        return 1;
  }

  pthread_join(gpio_thread_id,NULL);
  pthread_join(input_thread_id,NULL);
  pthread_join(ping_thread_id,NULL);

  return 0;
}


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define RECEIVE_PORT 14141 // pi1과 사진 파일을 통신하는 port
#define BUFFER_SIZE 1024

#define FILE_PATH "/home/ruby/Desktop/case/dataset/image.zip" // 저장할때 이름
#define EXTRACT_PATH "/home/ruby/Desktop/case/dataset/extracted" // 압축풀곳
#define PYTHON_PATH "/home/ruby/Desktop/raspi_dataset+learning.py" // 인공지능 학습 파이썬 코드


// with pi 1
void receive_file_and_extract(int port, const char *file_path, const char *extract_path) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];
    // server socket 만들기
    while (1) {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("Socket Create Failure");
        exit(EXIT_FAILURE);
    }
    //
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);


    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Binding Failure");
        exit(EXIT_FAILURE);
    }
     
     if (listen(server_fd, 3) < 0) {
        perror("Connection Waiting Failure");
        exit(EXIT_FAILURE);
    }
    printf("C server is waiting at port %d...\n", port);
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (new_socket < 0) {
            perror("Client Connection Failure");
            continue;
        }
        printf("Client connected.\n");


        FILE *file = fopen(file_path, "w");
        if (!file) {
            perror("File Open Failure");
            close(new_socket);
        }
        // 파일 받기
        int valread;
        while ((valread = read(new_socket, buffer, BUFFER_SIZE)) > 0) {
            fwrite(buffer, sizeof(char), valread, file);
        }
        close(new_socket);
        fclose(file);
        printf("ZIP file received and saved as '%s'.\n", file_path);
       
        const char *save_path = "/home/ruby/Desktop/case/dataset/extracted";
    char command[256];
     // 기존에 있던 사진 지기
    snprintf(command, sizeof(command), "rm -rf %s/*", save_path);
    if (system(command) == 0) {
        printf("All files in %s removed successfully.\n", save_path);
    } else {
        printf("Failed to remove files in %s\n.", save_path);
    }
        // ZIP 파일 압축 풀기
        char unzip_command[BUFFER_SIZE];
        snprintf(unzip_command, BUFFER_SIZE, "unzip -o %s -d %s", file_path, extract_path);
        if (system(unzip_command) != 0) {
            perror("Unzip command failed");
        } else {
            printf("ZIP file extracted successfully to '%s'.\n", extract_path);
            char command[512];
            snprintf(command, sizeof(command), "python %s", PYTHON_PATH);
            if (system(command) != 0) {
                fprintf(stderr, "Compilation failed.\n");
                close(server_fd);
            }
       		if(file_path != NULL){
		fflush(stdout);
		}
	 
            close(new_socket);
            close(server_fd);
        } 
        
    }
}



int main(){
    while(1){
    receive_file_and_extract(RECEIVE_PORT, FILE_PATH, EXTRACT_PATH);
    
    return 0;
    }
}

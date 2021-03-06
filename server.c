#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/in.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <pthread.h>
#include <semaphore.h>

#define MAX_ARRAY 1500
#define MAX_THREADS 4
#define QUEUE_SIZE 100

typedef struct {
    int client_fd;
} task;

sem_t mutex, thread_mutex;

task tasks[QUEUE_SIZE];
int task_fill_level = 0;
int threads_level = MAX_THREADS;

void write_error(char *arr);
void handle_connection(int);
void* thread_pool(void *);

int get_page(char[], char[]);
void clear_buffer(char[], int);
int m_strcmp(char[], char[]);
void m_strcat(char [], char[], char[]);
int is_image_requested(char[]);
off_t get_file_length(int);
void send_new(int, char[]);
int m_strlen(char[]);
void execute_php(char[], int, char[]);
int is_php(char[]);
void m_strcpy(char src[], char desc[]);

int main(){
    struct sockaddr_in server_addr, client_addr;
    socklen_t length = sizeof(client_addr);
    int pid;
    int server_fd, client_fd;
    int port = 5000;
    int on = 1;

    sem_init(&mutex, 0, 1);
    sem_init(&thread_mutex, 0, 0);

    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if(server_fd < 0){
	write_error("Could not create the socket");
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if(bind(server_fd, (struct sockaddr*) &server_addr,
	    sizeof(server_addr)) == -1){
	close(server_fd);
	write_error("Cannot bind");
    }

    
    if(listen(server_fd, 10) == -1){
	close(server_fd);
	write_error("cannot listen");
    }

    pthread_t threads[MAX_THREADS];
    int x;
    
    for(x = 0; x < QUEUE_SIZE; x++){
        tasks[x].client_fd = -1;
    }
    
    for(x = 0; x < MAX_THREADS; x++){
	if(pthread_create(&threads[x], NULL, &thread_pool, NULL) != 0){
	    perror("Thread creation failed");
	}
    }
    
    while(1){
	client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &length);

	if(client_fd == -1){
	    perror("Connection refused");
	}

	sem_wait(&mutex);

	if(task_fill_level < QUEUE_SIZE){
	    tasks[task_fill_level].client_fd = client_fd;
	
	    task_fill_level++;
	}else{
	    printf("Client dropped, queue is full\n");
	}
	
	sem_post(&mutex);
	sem_post(&thread_mutex);
	
    }

    sem_destroy(&mutex);
    sem_destroy(&thread_mutex);
    
    return 0;
}


void* thread_pool(void* args){
    while(1){
	task task;
	
	sem_wait(&thread_mutex);
	sem_wait(&mutex);
	
	task = tasks[0];
	int x;
	
	for(int x = 0; x < task_fill_level - 1; x++){
	    tasks[x] = tasks[x + 1];
	}

	task_fill_level--;
	
	sem_post(&mutex);
	
	handle_connection(task.client_fd);
	close(task.client_fd);
	
    }    
}

void send_new(int fd, char msg[]) {
    int len = m_strlen(msg);

    if (send(fd, msg, len, 0) == -1) {
	printf("Error in send\n");
    }
}

off_t get_file_length(int fd){
    struct stat file;

    if(fstat(fd, &file) == 0){
	return file.st_size;
    }

    return -1;
}

void write_error(char *arr){
    perror(arr);
    exit(1);
}

void execute_php(char file[], int client_fd, char buffer[]){
    send_new(client_fd,
	     "HTTP/1.1 200 OK\n Server: Web Server in C\n Connection: close\n");
    dup2(client_fd, STDOUT_FILENO);

    char script[500];
    m_strcpy("SCRIPT_FILENAME=", script);
    m_strcat(script, file, file);
    putenv("GATEWAY_INTERFACE=CGI/1.1");
    putenv(file);
    putenv("QUERY_STRING=");
    putenv("REQUEST_METHOD=GET");
    putenv("REDIRECT_STATUS=true");
    putenv("SERVER_PROTOCOL=HTTP/1.1");
    putenv("REMOTE_HOST=127.0.0.1");
    int pid = fork();
    if(pid == 0)
    execl("/usr/bin/php-cgi", "php-cgi", NULL);

}

void m_strcpy(char src[], char desc[]){
    int index = 0;

    while(src[index] != '\0'){
	desc[index] = src[index];
	index++;
    }
    
    desc[index] = '\0';
}

int m_strlen(char str[]){
    int index = 0;

    while(str[index] != '\0'){
	index++;
    }
    return index;
}

void handle_connection(int client_fd){
    char buffer[5000];
    char page_buffer[MAX_ARRAY];
    int filefd;
    off_t t;
    
    clear_buffer(buffer,1);
    clear_buffer(page_buffer,1);
    read(client_fd, buffer, 4999);
   
    get_page(buffer, page_buffer);

    
    if(is_php(page_buffer)){
	m_strcat("root/", page_buffer, page_buffer);
	
	filefd = open(page_buffer, O_RDONLY);

	if(filefd > 0){
	    close(filefd);

	    execute_php(page_buffer, client_fd, buffer);
	    sleep(1);
	}
       
    }else{
	if(is_image_requested(page_buffer)){
	    m_strcat("images/", page_buffer, page_buffer);
	}else{
	    m_strcat("root/", page_buffer, page_buffer);
	}
       
	filefd = open(page_buffer, O_RDONLY);

	
	if(filefd > 0){
	    t = get_file_length(filefd);
	    send_new(client_fd, "HTTP/1.1 200 OK\r\n");
	    send_new(client_fd, "Content-Length=4\r\n\r\n");


	    off_t offset = 0;

	    for(size_t size_to_send = t; size_to_send > 0; ){
		ssize_t sent = sendfile(client_fd, filefd, &offset,
					size_to_send);

		if(sent <= 0){
		    break;
		}
		size_to_send -= sent;

	    }

	    close(filefd);    
	}
	
    }

}

/*
Purpose: to locate and send back the page that the client requested from the
         server. 


Example: assume the client requested the page called home.html. The function
         will strip the home.html from the html protocol request and store it
         in the page array.
*/

int get_page(char client[], char page[]){
    int x;
    int i = 0;
    for(x = 5; client[x] != ' '; x++){
	page[i] = client[x];
	i++;
    }
    page[i+1] = '\0';
}

void clear_buffer(char buf[], int size){
    int x = 0;

    while(buf[x] != '\0'){
	buf[x] = '\0';
	x++;
    }
}

int m_strcmp(char arr1[], char arr2[]){
    int index = 0;
    
    while(arr1[index] != '\0' && arr2[index] != '\0'){
	if(arr1[index] != arr2[index]){
	    return 0;
	}
	index++;
    }

    return 1;
}


void m_strcat(char arr1[], char arr2[], char copied[]){
    char temp[MAX_ARRAY];
    int x = 0;
    int i = 0;
    
    while(arr1[x] != '\0'){
	temp[x] = arr1[x];
	x++;
    }

    while(arr2[i] != '\0'){
	temp[x] = arr2[i];
	x++;
	i++;
    }

    temp[x] = '\0';
    
    x = 0;

    while(temp[x] != '\0'){
	copied[x] = temp[x];
	x++;
    }
    copied[x] = '\0';
}

int is_image_requested(char file[]){
    char extension[8];
    int index = 0;
    int found = 0;
    int index2 = 0;
    
    while(file[index] != '\0'){

	if(file[index] == '.'){
	    found = 1;
	    index++;
	    break;
	}
	index++;
    }
    
    if(found == 1){
       
	while(file[index] != '\0'){
	    extension[index2] = file[index];
	    index++;
	    index2++;
	}
	extension[index2] = '\0';	

	if(m_strcmp(extension, "png")){
	    return 1;
	}else if(m_strcmp(extension, "jpeg")){
	    return 1;
	}else if(m_strcmp(extension, "jpg")){
	    return 1;
	} else if(m_strcmp(extension, "ico")){
	    
	    return 1;
	}else if(m_strcmp(extension, "gif")){
	    return 1;
	}
    }
    return 0;
}

int is_php(char file[]){
    char extension[8];
    int index = 0;
    int found = 0;
    int index2 = 0;

    
    while(file[index] != '\0'){

	if(file[index] == '.'){
	    found = 1;
	    index++;
	    break;
	}
	index++;
    }

    
    if(found == 1){
       
	while(file[index] != '\0'){
	    extension[index2] = file[index];
	    index++;
	    index2++;
	}
	extension[index2] = '\0';	

	if(m_strcmp(extension, "php")){
	    return 1;
	}
    }
    
   
    return 0;
}

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define SHM_REG_NAME "/registration"
#define SHM_REG_SIZE 50
#define SHM_CHAT_SIZE sizeof(message_t)
#define GROUP_NAME_PREFIX '#'
#define COMMAND_PREFIX '/'

typedef struct{
    char destination[100];
    char source[100];
    char message[256];
} message_t;

typedef struct{
    char shm_recv_name[100];
} chat_thread_args;

void* handle_memptr(const char* shm_name, size_t shm_size);
void* handle_listen_thread(void* args);

int main(){
    char login_username[50];
    pthread_t listen_thread;
    message_t msg = {};
    char* reg_memptr = (char*) handle_memptr(SHM_REG_NAME, SHM_REG_SIZE);

    printf("Введите имя пользователя для регистрации: ");
    scanf("%49s", login_username);
    strncpy(reg_memptr, login_username, SHM_REG_SIZE - 1);
    reg_memptr[SHM_REG_SIZE - 1] = '\0';
    printf("Вы зарегистрированы как: %s\n", login_username);
    char shm_send[100];
    char shm_recv[100];
    snprintf(shm_send, sizeof(shm_send), "/%s_send", login_username);
    snprintf(shm_recv, sizeof(shm_recv), "/%s_recv", login_username);

    message_t* chat_send_memptr = (message_t*) handle_memptr(shm_send, SHM_CHAT_SIZE);
    chat_thread_args *args = malloc(sizeof(chat_thread_args));
    strncpy(args->shm_recv_name, shm_recv, sizeof(args->shm_recv_name) - 1);
    args->shm_recv_name[sizeof(args->shm_recv_name) - 1] = '\0';
    if (pthread_create(&listen_thread, NULL, handle_listen_thread, (void*)args) != 0){
        perror("Ошибка создания потока");
        exit(EXIT_FAILURE);
    }
    printf("Поток прослушивания запущен.\n");

    while (1){
        printf("Введите имя получателя ('exit' для выхода) или перейдите в меню управления группой при помощи '/', для отправки сообщения в группу необходимо вступить в нее и в качестве получателя указать #Имя_Группы: ");
        scanf("%s", msg.destination);
        if (strcmp(msg.destination, "exit") == 0){
            break;
        }

        if (strncmp(msg.destination, "/", 1) == 0){
            printf("Введите команду (например, /create_group Friends или /join_group Friends): ");
            getchar();
            fgets(msg.message, sizeof(msg.message), stdin);
            msg.message[strcspn(msg.message, "\n")] = '\0';
        }
        else if (msg.destination[0] == GROUP_NAME_PREFIX){
            printf("Введите сообщение для группы %s: ", msg.destination);
            getchar();
            fgets(msg.message, sizeof(msg.message), stdin);
            msg.message[strcspn(msg.message, "\n")] = '\0';
        }
        else{
            printf("Введите сообщение для пользователя %s: ", msg.destination);
            getchar();
            fgets(msg.message, sizeof(msg.message), stdin);
            msg.message[strcspn(msg.message, "\n")] = '\0';
        }
        strncpy(chat_send_memptr->source, login_username, sizeof(chat_send_memptr->source) - 1);
        chat_send_memptr->source[sizeof(chat_send_memptr->source) - 1] = '\0';
        strncpy(chat_send_memptr->destination, msg.destination, sizeof(chat_send_memptr->destination) - 1);
        chat_send_memptr->destination[sizeof(chat_send_memptr->destination) - 1] = '\0';
        strncpy(chat_send_memptr->message, msg.message, sizeof(chat_send_memptr->message) - 1);
        chat_send_memptr->message[sizeof(chat_send_memptr->message) - 1] = '\0';

        printf("Сообщение отправлено.\n");
    }

    free(args);
    pthread_cancel(listen_thread);
    pthread_join(listen_thread, NULL);
    shm_unlink(shm_send);
    shm_unlink(shm_recv);
    munmap(chat_send_memptr, SHM_CHAT_SIZE);
    munmap(reg_memptr, SHM_REG_SIZE);
    printf("Клиент завершил работу.\n");
    return 0;
}
void* handle_memptr(const char* shm_name, size_t shm_size) {
    int fd = shm_open(shm_name, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        perror(shm_name);
        exit(EXIT_FAILURE);
    }
    if (ftruncate(fd, shm_size) == -1){
        perror("ftruncate");
        exit(EXIT_FAILURE);
    }
    void* memptr = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (memptr == MAP_FAILED) {
        perror("MMAP");
        exit(EXIT_FAILURE);
    }
    close(fd);
    memset(memptr, '\0', shm_size);
    return memptr;
}

void* handle_listen_thread(void* args){
    chat_thread_args* chat_args = (chat_thread_args*)args;
    message_t* chat_recv_memptr = (message_t*)handle_memptr(chat_args->shm_recv_name, SHM_CHAT_SIZE);

    while (1){
        if (chat_recv_memptr->message[0] != '\0'){
            if (chat_recv_memptr->destination[0] == GROUP_NAME_PREFIX){
                printf("\n[Группа %s] %s: %s\n", chat_recv_memptr->destination, chat_recv_memptr->source, chat_recv_memptr->message);
            }
            else{
                printf("\n%s: %s\n", chat_recv_memptr->source, chat_recv_memptr->message);
            }
            memset(chat_recv_memptr, '\0', SHM_CHAT_SIZE);
        }
        usleep(100000);
    }

    munmap(chat_recv_memptr, SHM_CHAT_SIZE);
    return NULL;
}

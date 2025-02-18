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
#define USER_STORE_SIZE 20
#define GROUP_NAME_PREFIX '#'
#define COMMAND_PREFIX '/'

typedef struct{
    char destination[100];
    char source[100];
    char message[256];
} message_t;

typedef struct{
    char shm_send_name[100];
    char shm_recv_name[100];
} chat_thread_args;

typedef struct {
    char name[100];
    char members[USER_STORE_SIZE][100];
    size_t member_count;
} group_t;

typedef struct {
    group_t groups[USER_STORE_SIZE];
    size_t group_count;
    pthread_mutex_t mutex;
} server_data_t;
server_data_t server_data = {.group_count = 0, .mutex = PTHREAD_MUTEX_INITIALIZER};

void* handle_memptr(const char* shm_name, size_t shm_size);
void* handle_chat_thread(void* arg);
void create_group(const char* group_name);
void join_group(const char* group_name, const char* username);

int main(){
    pthread_t threads[USER_STORE_SIZE];
    size_t threads_counter = 0;
    char* reg_memptr = (char*)handle_memptr(SHM_REG_NAME, SHM_REG_SIZE);
    char** user_store = malloc(USER_STORE_SIZE * sizeof(char*));
    chat_thread_args *args;
    size_t user_count = 0;

    printf("Сервер запущен и ожидает регистрации пользователей...\n");
    while (1){
        char username[50];
        strncpy(username, reg_memptr, sizeof(username) - 1);
        username[sizeof(username) - 1] = '\0';
        if (username[0] != '\0' && user_count < USER_STORE_SIZE){
            memset(reg_memptr, '\0', SHM_REG_SIZE);
            printf("Новый пользователь: %s\n", username);
            user_store[user_count++] = strdup(username);

            args = malloc(sizeof(chat_thread_args));
            snprintf(args->shm_send_name, sizeof(args->shm_send_name), "/%s_send", username);
            snprintf(args->shm_recv_name, sizeof(args->shm_recv_name), "/%s_recv", username);

            if (pthread_create(&threads[threads_counter++], NULL, handle_chat_thread, (void*)args) != 0){
                perror("Ошибка создания потока");
                exit(EXIT_FAILURE);
            }
            printf("Поток для %s создан.\n", username);
        }
        usleep(100000);
    }

    for(size_t i = 0; i < user_count; ++i){
        free(user_store[i]);
    }
    free(user_store);
    free(args);
    munmap(reg_memptr, SHM_REG_SIZE);
    shm_unlink(SHM_REG_NAME);
    return 0;
}

void* handle_chat_thread(void* arg){
    chat_thread_args* args = (chat_thread_args*)arg;
    printf("Поток прослушивания для %s создан.\n", args->shm_send_name);
    message_t* chat_send_memptr = (message_t*)handle_memptr(args->shm_send_name, SHM_CHAT_SIZE);
    message_t message;

    while (1){
        if (chat_send_memptr->destination[0] != '\0'){
            memcpy(&message, chat_send_memptr, sizeof(message_t));
            memset(chat_send_memptr, '\0', SHM_CHAT_SIZE);

            printf("Получено сообщение от %s для %s: %s\n", message.source, message.destination, message.message);

            if (message.destination[0] == COMMAND_PREFIX){
                if (strncmp(message.destination, "/create_group", strlen("/create_group")) == 0){
                    create_group(message.message);
                    printf("Группа %s создана.\n", message.message);
                }
                else if (strncmp(message.destination, "/join_group", strlen("/join_group")) == 0){
                    join_group(message.message, message.source);
                    printf("Пользователь %s присоединился к группе %s.\n", message.source, message.message);
                }
            }
            else if (message.destination[0] == GROUP_NAME_PREFIX){
                char group_name[100];
                strncpy(group_name, message.destination + 1, sizeof(group_name) - 1);
                group_name[sizeof(group_name) - 1] = '\0';
                pthread_mutex_lock(&server_data.mutex);
                int group_found = 0;
                for(size_t i = 0; i < server_data.group_count; ++i){
                    if (strcmp(server_data.groups[i].name, group_name) == 0){
                        group_found = 1;
                        for(size_t j = 0; j < server_data.groups[i].member_count; ++j){
                            if (strcmp(server_data.groups[i].members[j], message.source) != 0){
                                char shm_dest_recv[110];
                                snprintf(shm_dest_recv, sizeof(shm_dest_recv), "/%s_recv", server_data.groups[i].members[j]);
                                printf("Отправка группового сообщения получателю: %s\n", shm_dest_recv);
                                message_t* dest_memptr = (message_t*)handle_memptr(shm_dest_recv, SHM_CHAT_SIZE);
                                memcpy(dest_memptr, &message, sizeof(message_t));
                                munmap(dest_memptr, SHM_CHAT_SIZE);
                                printf("Групповое сообщение отправлено получателю: %s\n", server_data.groups[i].members[j]);
                            }
                        }
                        break;
                    }
                }
                if (!group_found){
                    printf("Группа %s не найдена.\n", group_name);
                }
                pthread_mutex_unlock(&server_data.mutex);
            }
            else{
                char shm_dest_recv[110];
                snprintf(shm_dest_recv, sizeof(shm_dest_recv), "/%s_recv", message.destination);
                printf("Отправка сообщения получателю: %s\n", shm_dest_recv);
                message_t* dest_memptr = (message_t*)handle_memptr(shm_dest_recv, SHM_CHAT_SIZE);
                memcpy(dest_memptr, &message, sizeof(message_t));
                munmap(dest_memptr, SHM_CHAT_SIZE);
                printf("Сообщение отправлено получателю: %s\n", message.destination);
            }
        }
        usleep(100000);
    }
    free(args);
    return NULL;
}

void create_group(const char* group_name){
    pthread_mutex_lock(&server_data.mutex);
    for(size_t i = 0; i < server_data.group_count; ++i){
        if (strcmp(server_data.groups[i].name, group_name) == 0){
            printf("Группа %s уже существует.\n", group_name);
            pthread_mutex_unlock(&server_data.mutex);
            return;
        }
    }
    if (server_data.group_count < USER_STORE_SIZE){
        strncpy(server_data.groups[server_data.group_count].name, group_name, sizeof(server_data.groups[server_data.group_count].name) - 1);
        server_data.groups[server_data.group_count].name[sizeof(server_data.groups[server_data.group_count].name) - 1] = '\0';
        server_data.groups[server_data.group_count].member_count = 0;
        server_data.group_count++;
        printf("Группа %s успешно создана.\n", group_name);
    }
    pthread_mutex_unlock(&server_data.mutex);
}

void join_group(const char* group_name, const char* username){
    pthread_mutex_lock(&server_data.mutex);
    for(size_t i = 0; i < server_data.group_count; ++i){
        if (strcmp(server_data.groups[i].name, group_name) == 0){
            int already_member = 0;
            for(size_t j = 0; j < server_data.groups[i].member_count; ++j){
                if (strcmp(server_data.groups[i].members[j], username) == 0){
                    already_member = 1;
                    break;
                }
            }
            if (!already_member && server_data.groups[i].member_count < USER_STORE_SIZE){
                strncpy(server_data.groups[i].members[server_data.groups[i].member_count], username, sizeof(server_data.groups[i].members[server_data.groups[i].member_count]) - 1);
                server_data.groups[i].members[server_data.groups[i].member_count][sizeof(server_data.groups[i].members[server_data.groups[i].member_count]) - 1] = '\0';
                server_data.groups[i].member_count++;
                printf("Пользователь %s успешно присоединился к группе %s.\n", username, group_name);
            }
            else{
                printf("Пользователь %s уже состоит в группе %s или группа полна.\n", username, group_name);
            }
            pthread_mutex_unlock(&server_data.mutex);
            return;
        }
    }
    pthread_mutex_unlock(&server_data.mutex);
    printf("Группа %s не найдена.\n", group_name);
}

void* handle_memptr(const char* shm_name, size_t shm_size) {
    int fd = shm_open(shm_name, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        perror("SHM_OPEN");
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
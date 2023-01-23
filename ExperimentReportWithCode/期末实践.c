#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <dirent.h>
#include <fcntl.h>                          
#include <sys/types.h>                        
#include <sys/stat.h>                       

#define BUFSIZE 1024  //输入缓冲区大小
#define TOKENNUM 64  //默认最多的输入单词数量
#define pipeNum 16
#define pipeBuf 64
#define PIPE_FILE "./pipe_file.txt"  // 作为管道的文件
#define COMNUM 16  //默认一行的命令数目最多是16

//内部命令表
char* inner_commands[] = {"cd", "pwd", "help", "exit", "echo", "ls", "env", "export"};
int inner_commands_num = 8;
int fds[2];  //管道命令的工具
long timesEnd[pipeNum];  //标识第i个管道命令执行之后的文件大小
char myenvs[8][32];  //记录自己创建的环境变量
int myenvNum = 0;  //自己创建的环境变量的个数

//统计文件的字节数目
long countPipeFile(){
    long fileSize = 0;
    FILE* pFile = fopen(PIPE_FILE, "r");
    if(pFile == NULL){
        printf("Open File Error!\n");
        return 0;
    }
    fseek(pFile, 0, SEEK_END);  //指针移向文件末尾
    fileSize = ftell(pFile);
    fclose(pFile);
    return fileSize;
}

int getuserdir(char *aoUserDir)
{
    char *LoginId;
    struct passwd *pwdinfo;
    if (aoUserDir == NULL)
        return -9;
    if ((LoginId = getlogin ()) == NULL) {
        perror ("getlogin");
        aoUserDir[0] = '\0';
        return -8;
    }
    if ((pwdinfo = getpwnam (LoginId)) == NULL) {
        perror ("getpwnam");
        return -7;
    }
    strcpy (aoUserDir, pwdinfo->pw_dir);
}

int func_cd(char** tokens){
    //正常来说，cd只有两个串
    char* buffer = malloc(sizeof(char) * BUFSIZE);
    if(tokens[1] == NULL){
        //一个串返回登录目录 
        getuserdir(buffer);
        tokens[1] = buffer;
    }
    //系统调用chdir，执行成功返回0
    if(chdir(tokens[1]) != 0){
        perror("myshell");
    }
    free(buffer);
    return 1;
}

int func_pwd(char** tokens){
    char* buffer = malloc(sizeof(char) * BUFSIZE);  //接收路径
    getcwd(buffer, BUFSIZE);
    printf("%s\n", buffer);
    free(buffer);
    return 1;
}

//help用于调用内部命令的集合
int func_help(char** tokens){
    printf("--------------- YWL's Mini Shell ---------------\n");
    printf("These shell commands are defined internally.  Type `help' to see this list.\n");
    for (int i = 0; i < inner_commands_num; i++)
    {
        printf("%s\n", inner_commands[i]);
    }
    return 1;
}

int func_exit(char** tokens){
    printf("--------------- Goodbye! ---------------\n");
    return 0;
}

int func_echo(char** tokens){
    //echo指令还需要细分
    if(tokens[1][0] == '$'){
        int i = 0;
        while(tokens[1][i] != '\0'){
            tokens[1][i] = tokens[1][i+1];
            i++;
        }
        printf("%s\n",getenv(tokens[1]));  //用来获得特定的环境变量
    }else{
        int i = 1;
        while(tokens[i] != NULL){
            printf("%s ", tokens[i]);
            i++;
        }
        printf("\n");
    }
    return 1;
}

int func_ls(char** tokens){
    //首先获得当前的路径，用getcwd
    char* path = malloc(sizeof(char) * BUFSIZE);  //接收路径
    getcwd(path, BUFSIZE);
    DIR* dir;
    struct dirent * ptr;
    if((dir=opendir(path)) == NULL){
      perror("Open dir error");
      exit(1);
    }
    while((ptr=readdir(dir)) != NULL){
        if(strcmp(ptr->d_name,".")==0 || strcmp(ptr->d_name,"..")==0)    ///current dir OR parrent dir
            continue;
        printf("%s\n",ptr->d_name);
    }
    closedir(dir);
    free(path);
    return 1;
}

int func_env(char** tokens){
    extern char** environ;
    int i = 0;
    for(; environ[i]; i++)
    {
       printf("%s\n",environ[i]);
    }
    return 1;
}

int func_export(char** tokens){
    if(tokens[1] == NULL){
        printf("please input correct env\n");
    }else{
        strcpy(myenvs[myenvNum], tokens[1]);
        int ret = putenv(myenvs[myenvNum]);
        if (ret == 0){
            printf("%s export success\n", myenvs[myenvNum]);
        }
        myenvNum++;
    }
    return 1;
}

//函数指针数组
int (*funcs[])(char**) = {&func_cd, &func_pwd, &func_help, &func_exit, &func_echo, &func_ls, &func_env, &func_export};

//读入整行命令
char* shell_readline(){
    int i = 0;
    char* buffer = malloc(sizeof(char) * BUFSIZE);
    int c;  //接收字符ACSII码值
    if(!buffer){
        printf("allocation failed!\n");
        exit(1);
    }
    while(1){
        c = getchar();
        if(c == EOF || c == '\n'){
            buffer[i] = '\0';
            return buffer;  //遇到错误和换行都结束
        }
        else{
            buffer[i] = c;
            i++;
        }
    }
}

//输入的命令需要切分
char** split_line(char* line){
    int i = 0;
    char** tokens = malloc(sizeof(char*) * TOKENNUM);
    char* token;
    //分配失败
    if(!tokens){
        printf("allocation failed!\n");
        exit(1);
    }
    token = strtok(line, " ");
    while (token != NULL)
    {
        tokens[i] = token;
        i++;
        //理论上允许用户输入无限个单词，所以当输入单词超过预定的64个之后，应该重新分配
        //但是本次实验就假设输入单词个数在64之内
        token = strtok(NULL, " ");  //继续用之前的line
    }
    tokens[i] = NULL;  //终结符号
    return tokens;
}

int outter_commands(char** tokens, int andflag){
    int pid = fork();
    int status;
    if(pid < 0){
        fprintf(stderr, "Fork Failed\n");
    }
    else if (pid == 0){
        if (execvp(tokens[0], tokens) == -1){
            perror("myshell ");
            exit(1);
        }
    }
    else {
        if(!andflag){
            while(1){
                pid = wait(&status);
                if(pid == -1){
                    break;
                }
                else{
                    WEXITSTATUS (status);
                }
            }
        }else{
            printf("进入后台执行的进程号是: %d\n", pid);
        }
    }
    return 1;
}

int execute(char** tokens, int andflag){  //如果andflag == 1，就创建一个子线程执行，并且主线程不等待
    if(tokens[0] == NULL){
        //没有命令
        return 1;
    }
    //尝试逐个匹配内部命令
    for(int i=0;i<inner_commands_num;i++){
        if(strcmp(tokens[0], inner_commands[i]) == 0){
            //执行对应的内部命令
            if(inner_commands[i] == "exit"){  //exit命令没有必要后台执行，不管什么情况都直接在主线程执行就好
                return (*funcs[i])(tokens);
            }else{
                if(andflag){
                    //创建一个子进程去执行命令
                    int pid = fork();
                    int status;
                    if(pid < 0){
                        fprintf(stderr, "Fork Failed\n");
                    }
                    else if (pid == 0){
                        //子进程执行指令
                        (*funcs[i])(tokens);
                        //执行之后直接结束进程
                        exit(1);
                    }
                    else {
                        //主进程不等待
                        printf("进入后台执行的进程号是: %d\n", pid);
                        return 1;  //除了exit之外，全都会继续执行
                    }
                }else{
                    return (*funcs[i])(tokens);
                }
            }
        }
    }
    //如果没有匹配的内部命令，则作为外部命令处理
    return outter_commands(tokens, andflag);
}

//将管道命令根据管道符|切割
char** cut_pipeCommands(char* line, int* pipeComNum){
    //printf("管道命令是: %s\n", line);
    char** pipeCommands = malloc(sizeof(char*) * pipeNum);
    for(int i=0;i<pipeNum;i++){
        pipeCommands[i] = malloc(sizeof(char)*pipeBuf);
    }
    int i=0,j=0,k=0;
    while(line[i] != '\0'){
        if(line[i] != ' ' && line[i] != '|'){
            pipeCommands[j][k] = line[i];
            i++;
            k++;
        }else if(line[i] == ' ' && line[i+1] == '|'){
            pipeCommands[j][k] = '\0';
            i+=3;
            j++;
            k=0;
        }
    }
    pipeCommands[j][k] = '\0';
    (*pipeComNum) = j + 1;  //把命令的数量给传回去
    return pipeCommands;
}

//执行具体的管道命令
void runPipeCommands(int begin, int now, int end, char** commands){
    int savedIn,savedOut;
    if(begin != now){
        runPipeCommands(begin, now-1, end, commands);
    }
    if(now != begin){
        long offsize = timesEnd[now - 2];
        //printf("第%d个命令的偏移量是:%ld\n", now, offsize);
        fds[0] = open(PIPE_FILE, O_CREAT|O_RDONLY, 0666);
        lseek(fds[0], (off_t)offsize, SEEK_SET);
        savedIn = dup(fileno(stdin));
        close(fileno(stdin));
        dup2(fds[0], fileno(stdin));
    }
    if(now != end){
        fds[1] = open(PIPE_FILE, O_CREAT|O_WRONLY|O_APPEND, 0666);
        savedOut = dup(fileno(stdout));
        close(fileno(stdout));
        dup2(fds[1], fileno(stdout));
    }
    char** tokens = split_line(commands[now-1]);
    execute(tokens, 0);
    long filesize = countPipeFile();
    timesEnd[now] = filesize;
    //printf("第%d个命令执行完,文件大小是:%ld\n", now, timesEnd[now]);
    if(now != begin){
        dup2(savedIn, fileno(stdin));
        close(fds[0]);
    }
    if(now != end){
        dup2(savedOut, fileno(stdout));
        close(fds[1]);
    }
}

//执行管道命令的函数
int commandWithPipe(char* line){
    int pipeComNum = 0;
    int flag;
    char** pipeCommands = cut_pipeCommands(line, &pipeComNum);

    for(int i=0;i<=pipeComNum;i++){
        timesEnd[i] = 0;  //初始化
    }
    
    //递归执行管道命令
    runPipeCommands(1, pipeComNum, pipeComNum, pipeCommands);

    for(int i=0;i<pipeNum;i++){
        free(pipeCommands[i]);
    }
    free(pipeCommands);
    //管道命令执行完毕之后别忘了删除管道文件的内容
    FILE* file;
	file = fopen(PIPE_FILE,"w");//清空当前文件夹下的test.txt文件
    fclose(file);
    return 1;
}

//处理重定向命令的函数
void commandWithRedi(char** tokens, int rediflag){
    //获取执行的命令，把tokens最后两项变成NULL即可
    int i = 0;
    while(tokens[i] != NULL){
        i++;
    }
    char* filename = tokens[i-1];  //token[i-1]是文件
    tokens[i-1] = NULL;
    tokens[i-2] = NULL;
    if(rediflag == 1){
        //重定向>的解释执行
        //尝试逐个匹配内部命令
        for(int i=0;i<inner_commands_num;i++){
            if(strcmp(tokens[0], inner_commands[i]) == 0){
                //执行对应的内部命令，执行前重定向
                int fds = open(filename, O_CREAT|O_WRONLY|O_TRUNC, 0666);
                int savedOut = dup(fileno(stdout));
                close(fileno(stdout));
                dup2(fds, fileno(stdout));
                (*funcs[i])(tokens);
                //执行完定位回来
                dup2(savedOut, fileno(stdout));
                close(fds);
                return;  //可以直接结束了
            }
        }
        //如果没有匹配的内部命令，则作为外部命令处理
        int pid = fork();
        int status;
        if(pid < 0){
            fprintf(stderr, "Fork Failed\n");
        }
        else if (pid == 0){
            //在子进程里面进行重定向
            int fds = open(filename, O_CREAT|O_WRONLY|O_TRUNC, 0666);
            int savedOut = dup(fileno(stdout));
            close(fileno(stdout));
            dup2(fds, fileno(stdout));
            execvp(tokens[0], tokens);
            //执行完进程直接结束了，不用定向回来了
        }
        else {
            while(1){
                pid = wait(&status);
                if(pid == -1){
                    break;
                }
                else{
                    WEXITSTATUS (status);
                }
            }
        }
    }else if(rediflag==2){
        //重定向>>的解释执行
        //尝试逐个匹配内部命令
        for(int i=0;i<inner_commands_num;i++){
            if(strcmp(tokens[0], inner_commands[i]) == 0){
                //执行对应的内部命令，执行前重定向
                int fds = open(filename, O_CREAT|O_WRONLY|O_APPEND, 0666);
                int savedOut = dup(fileno(stdout));
                close(fileno(stdout));
                dup2(fds, fileno(stdout));
                (*funcs[i])(tokens);
                //执行完定位回来
                dup2(savedOut, fileno(stdout));
                close(fds);
                return;  //可以直接结束了
            }
        }
        //如果没有匹配的内部命令，则作为外部命令处理
        int pid = fork();
        int status;
        if(pid < 0){
            fprintf(stderr, "Fork Failed\n");
        }
        else if (pid == 0){
            //在子进程里面进行重定向
            int fds = open(filename, O_CREAT|O_WRONLY|O_APPEND, 0666);
            int savedOut = dup(fileno(stdout));
            close(fileno(stdout));
            dup2(fds, fileno(stdout));
            execvp(tokens[0], tokens);
            //执行完进程直接结束了，不用定向回来了
        }
        else {
            while(1){
                pid = wait(&status);
                if(pid == -1){
                    break;
                }
                else{
                    WEXITSTATUS (status);
                }
            }
        }
    }else if(rediflag==3){
        //重定向<的解释执行
        //内部命令不需要输入，可以直接跳过，作为外部命令处理
        int pid = fork();
        int status;
        if(pid < 0){
            fprintf(stderr, "Fork Failed\n");
        }
        else if (pid == 0){
            //在子进程里面进行重定向
            int fds = open(filename, O_CREAT|O_RDONLY, 0666);
            int savedIn = dup(fileno(stdin));
            close(fileno(stdin));
            dup2(fds, fileno(stdin));
            execvp(tokens[0], tokens);
            //执行完进程直接结束了，不用定向回来了
        }
        else {
            while(1){
                pid = wait(&status);
                if(pid == -1){
                    break;
                }
                else{
                    WEXITSTATUS (status);
                }
            }
        }
    }else{
        ;
    }
}

int execute_line(char* line){
    //先识别有几个封号
    char** cmds = malloc(sizeof(char*) * 8);
    for (int i = 0; i < 8; i++)
    {
        cmds[i] = malloc(sizeof(char) * 128);
    }
    int i = 0, j = 0, k=0;
    while(line[i] != '\0'){
        if(line[i] == ';'){
            cmds[j][k] = '\0';
            i++;
            j++;
            k = 0;
        }else{
            if(line[i] == '\t'){
                line[i] = ' ';
            }
            cmds[j][k] = line[i];
            i++;
            k++;
        }
    }
    cmds[j][k] = '\0';
    int cmd_num = j + 1;

    int flag = 1;  //标识是否需要退出(也就是是否是exit命令)
    int pipeflag = 0;  //标识是不是管道命令，管道命令需要单独处理
    int rediflag = 0;  //标识是不是重定向命令，重定向命令单独处理，0表示不是重定向，如果是重定向：1对应>，2对应>>，3对应<
    int andflag;
    //逐个执行命令
    for (int i = 0; i < cmd_num; i++)
    {
        andflag = 0;
        //识别管道命令
        for (int j = 0; j < strlen(cmds[i]); j++)
        {
            if(cmds[i][j] == '|' && cmds[i][j+1] == ' ' && cmds[i][j-1] == ' '){
                //满足这种格式说明是管道命令
                flag = commandWithPipe(cmds[i]);
                pipeflag = 1;
                break;
            }
        }
        if(pipeflag == 0){
            //不是管道命令的话
            //识别重定向命令
            for (int j = 0; j < strlen(cmds[i]); j++)
            {
                if(cmds[i][j] == '>' && cmds[i][j+1] != '>'){
                    rediflag = 1;
                    break;
                }else if(cmds[i][j] == '>' && cmds[i][j+1] == '>'){
                    rediflag = 2;
                    break;
                }else if(cmds[i][j] == '<'){
                    rediflag = 3;
                    break;
                }else{
                    ;
                }
            }
            if(rediflag == 0){
                //也不是重定向命令
                if(cmds[i][strlen(cmds[i])-1] == '&'){
                    andflag = 1;  //说明该命令需要后台执行
                    cmds[i][strlen(cmds[i])-1] = ' ';
                }
                char** tokens = split_line(cmds[i]);
                flag = execute(tokens, andflag);
            }else{
                //说明是重定向命令，调用处理重定向命令的函数，同时传入rediflag作为标识
                char** tokens = split_line(cmds[i]);
                commandWithRedi(tokens, rediflag);
                flag = 1;  //重定向命令执行之后需要继续shell
            }
        }
        if(flag == 0){
            break;
        }
        pipeflag = 0;
        rediflag = 0;
    } 
    for (int i = 0; i < 8; i++)
    {
        free(cmds[i]);
    }
    free(cmds);
    return flag;
}

void shell_loop(){
    //首先需要获取当前路径和用户名
    //用户名
    struct passwd *pwd;
    pwd = getpwuid(getuid());
    char* user_name = pwd->pw_name;
    //路径
    char* path = malloc(sizeof(char) * BUFSIZE); 
    char* line = malloc(sizeof(char) * BUFSIZE);  //接收的字符串长度在1024以内
    int flag = 1;  //循环是否终止的标识
    while(flag == 1){
        memset(line,'\0',(sizeof(char) * BUFSIZE));
        getcwd(path, BUFSIZE);
        
        printf("\033[;32m%s\033[0m", user_name);
        printf("\033[;37m:\033[0m");
        printf("\033[;34m%s\033[0m", path);
        printf("\033[;37m$ \033[0m");

        line = shell_readline();
        //读入命令之后，需要执行
        flag = execute_line(line);
    }
    free(line);
    free(path);
}

int main(){
    printf("************ Welcome to YWL's Mini Shell! ************\n");
    shell_loop();  //开启shell循环
    printf("************ YWL's Mini Shell Exit! ************\n");
    return 0;
}
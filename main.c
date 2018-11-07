
/* my-shell */
/* main.c */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define INITIAL_TOKEN_CAPACITY          16
#define TOKEN_CAPACITY_INCREMENT        16

#define INITIAL_COMMAND_CAPACITY        2
#define COMMAND_CAPACITY_INCREMENT      2

#define INITIAL_ARGUMENT_CAPACITY       3
#define ARGUMENT_CAPACITY_INCREMENT     3

enum TokenizeState {
    TokenizeStateNone,
    TokenizeStateToken,
    TokenizeStateSingleQuotedString,
    TokenizeStateDoubleQuotedString
};

enum TokenType {
    TokenTypeNone,
    TokenTypeGreat,
    TokenTypeLess,
    TokenTypeGreatGreat,
    TokenTypeGreatAnd,
    TokenTypePipe,
    TokenTypeSemicolon,
    TokenTypeAnd,
    TokenTypeAndAnd,
    TokenTypeOr,
    TokenTypeArgument
};

/*
 * <Command> ::= <SimpleCommand> [<Pipe> <SimpleCommand>]* <IsBackgroundExecution>?
 * <SimpleCommand> ::= <Argument> [<Argument> | <Redirect>]+
 * <Argument> ::= <Word>
 * <Redirect> ::=
 *     <Great> <Word> | <Less> <Word> |
 *     <GreatGreat> <Word> | <GreatAnd> <Word>
 * <Great> ::= ">"
 * <Less> ::= "<"
 * <GreatGreat> ::= ">>"
 * <GreatAnd> ::= ">&"
 * <Pipe> ::= "|"
 * <IsBackgroundExecution> ::= "&"
 */

enum ParseState {
    ParseStateNone,
    ParseStateCommandList,
    ParseStateArgumentList,
    ParseStateRedirect,
    ParseStateBackgroundExecution
};

struct TokenArray {
    char** Tokens;
    size_t Count;
};

struct SimpleCommandInfo {
    char** Args;
    size_t Argc;
};

struct RedirectInfo {
    char* RedirectInputFileName;
    char* RedirectOutputFileName;
    char* RedirectStderrFileName;
};

struct CommandInfo {
    struct SimpleCommandInfo* SimpleCommands;
    size_t SimpleCommandCount;
    struct RedirectInfo Redirect;
    bool IsBackgroundExecution;
};

bool TokenizeInput(char* inputBuffer, size_t inputLength, struct TokenArray* tokenArray);
char* CreateToken(const char* inputBuffer, size_t tokenLength, size_t tokenBeginIndex);
void FreeInputTokens(char** inputTokens, int numOfTokens);
enum TokenType GetTokenType(const char* tokenStr);

bool ParseCommand(struct TokenArray* tokenArray, struct CommandInfo* commandInfo);
void FreeSimpleCommands(struct SimpleCommandInfo* simpleCommands, size_t numOfSimpleCommands);
bool ParseSimpleCommand(
    struct TokenArray* tokenArray, size_t* tokenIndex,
    struct SimpleCommandInfo* simpleCommand, struct RedirectInfo* redirectInfo);
bool ParseRedirect(
    struct TokenArray* tokenArray, size_t* tokenIndex,
    struct RedirectInfo* redirectInfo);

void ExecuteCommand(struct CommandInfo* commandInfo, bool* isClosed);

void BuiltinCd(int argc, char** args, bool* isClosed);
void BuiltinPwd(int argc, char** args, bool* isClosed);
void BuiltinHelp(int argc, char** args, bool* isClosed);
void BuiltinExit(int argc, char** args, bool* isClosed);

typedef void (*BuiltinCommand)(int argc, char** args, bool* isClosed);

struct BuiltinCommandTable {
    char* CommandName;
    BuiltinCommand Func;
};

struct BuiltinCommandTable BuiltinCommands[] = {
    { "cd",   BuiltinCd },
    { "pwd",  BuiltinPwd },
    { "help", BuiltinHelp },
    { "exit", BuiltinExit }
};

size_t NumOfBuiltinCommands = sizeof(BuiltinCommands) / sizeof(struct BuiltinCommandTable);

int main(int argc, char** argv)
{
    char* inputBuffer;
    size_t bufferLength;
    ssize_t numOfRead;
    size_t i;
    size_t j;
    struct TokenArray tokenArray;
    struct CommandInfo commandInfo;
    bool isClosed;

    while (1) {
        /* Retrieve user input */
        inputBuffer = NULL;
        bufferLength = 0;

        printf(">> ");

        if ((numOfRead = getline(&inputBuffer, &bufferLength, stdin)) == -1)
            break;
        
        if (!TokenizeInput(inputBuffer, numOfRead, &tokenArray)) {
            fprintf(stderr, "TokenizeInput() failed\n");
            free(inputBuffer);
            continue;
        }
        
        /*
        for (i = 0; i < tokenArray.Count; ++i)
            printf("Token %zu: %s\n", i, tokenArray.Tokens[i]);
        */

        if (!ParseCommand(&tokenArray, &commandInfo)) {
            fprintf(stderr, "ParseCommand() failed\n");
            FreeInputTokens(tokenArray.Tokens, tokenArray.Count);
            free(inputBuffer);
            continue;
        }
        
        /*
        printf("CommandInfo.SimpleCommandCount: %zu\n", commandInfo.SimpleCommandCount);
        printf("CommandInfo.IsBackgroundExecution: %d\n", commandInfo.IsBackgroundExecution);

        for (i = 0; i < commandInfo.SimpleCommandCount; ++i) {
            printf("Command %zu: ", i);
            printf("Argc: %zu Args: ", commandInfo.SimpleCommands[i].Argc);

            for (j = 0; j < commandInfo.SimpleCommands[i].Argc; ++j)
                printf("%s ", commandInfo.SimpleCommands[i].Args[j]);
            
            printf("\n");
        }

        printf("RedirectInputFileName: %s\n", commandInfo.Redirect.RedirectInputFileName);
        printf("RedirectOutputFileName: %s\n", commandInfo.Redirect.RedirectOutputFileName);
        */

        ExecuteCommand(&commandInfo, &isClosed);
        
        FreeSimpleCommands(commandInfo.SimpleCommands, commandInfo.SimpleCommandCount);
        FreeInputTokens(tokenArray.Tokens, tokenArray.Count);
        free(inputBuffer);

        if (isClosed)
            break;
    }

    return EXIT_SUCCESS;
}

bool TokenizeInput(char* inputBuffer, size_t inputLength, struct TokenArray* tokenArray)
{
    size_t i;
    size_t tokenCapacity = INITIAL_TOKEN_CAPACITY;
    size_t tokenBeginIndex = 0;
    size_t tokenLength = 0;
    size_t numOfTokens = 0;
    
    char** inputTokens = NULL;
    char** newInputTokens = NULL;
    char* tokenBuffer = NULL;

    enum TokenizeState currentState = TokenizeStateNone;

    inputTokens = (char**)calloc(tokenCapacity, sizeof(char*));

    if (inputTokens == NULL)
        return false;

    for (i = 0; i < inputLength; ++i) {
        switch (currentState) {
            case TokenizeStateNone:
                if (inputBuffer[i] == '"') {
                    /* TODO: Handle escape sequence */
                    currentState = TokenizeStateDoubleQuotedString;
                    tokenBeginIndex = i + 1;
                    continue;
                } else if (inputBuffer[i] == '\'') {
                    currentState = TokenizeStateSingleQuotedString;
                    tokenBeginIndex = i + 1;
                    continue;
                } else if (!isspace(inputBuffer[i])) {
                    currentState = TokenizeStateToken;
                    tokenBeginIndex = i;

                    /* Reprocess the same character */
                    --i;

                    continue;
                }
                break;
            case TokenizeStateToken:
                if (isspace(inputBuffer[i])) {
                    tokenLength = i - tokenBeginIndex;
                    tokenBuffer = CreateToken(inputBuffer, tokenLength, tokenBeginIndex);

                    if (tokenBuffer == NULL) {
                        FreeInputTokens(inputTokens, numOfTokens);
                        return false;
                    }

                    inputTokens[numOfTokens] = tokenBuffer;
                    ++numOfTokens;

                    currentState = TokenizeStateNone;
                }
                break;
            case TokenizeStateSingleQuotedString:
                if (inputBuffer[i] == '\'') {
                    tokenLength = i - tokenBeginIndex;
                    tokenBuffer = CreateToken(inputBuffer, tokenLength, tokenBeginIndex);

                    if (tokenBuffer == NULL) {
                        FreeInputTokens(inputTokens, numOfTokens);
                        return false;
                    }

                    inputTokens[numOfTokens] = tokenBuffer;
                    ++numOfTokens;

                    currentState = TokenizeStateNone;
                }
                break;
            case TokenizeStateDoubleQuotedString:
                if (inputBuffer[i] == '"') {
                    tokenLength = i - tokenBeginIndex;
                    tokenBuffer = CreateToken(inputBuffer, tokenLength, tokenBeginIndex);

                    if (tokenBuffer == NULL) {
                        FreeInputTokens(inputTokens, numOfTokens);
                        return false;
                    }

                    inputTokens[numOfTokens] = tokenBuffer;
                    ++numOfTokens;

                    currentState = TokenizeStateNone;
                }
                break;
        }

        if (numOfTokens >= tokenCapacity) {
            tokenCapacity += TOKEN_CAPACITY_INCREMENT;
            newInputTokens = (char**)realloc(inputTokens, sizeof(char*) * tokenCapacity);

            if (newInputTokens == NULL) {
                FreeInputTokens(inputTokens, numOfTokens);
                return false;
            }

            inputTokens = newInputTokens;
        }
    }

    inputTokens[numOfTokens] = NULL;

    tokenArray->Tokens = inputTokens;
    tokenArray->Count = numOfTokens;

    return true;
}

char* CreateToken(const char* inputBuffer, size_t tokenLength, size_t tokenBeginIndex)
{
    char* tokenBuffer = NULL;

    /*
    tokenBuffer = (char*)calloc(tokenLength + 1, sizeof(char));

    if (tokenBuffer == NULL)
        return NULL;

    strncpy(tokenBuffer, inputBuffer + tokenBeginIndex, tokenLength);
    tokenBuffer[tokenLength] = '\0';
    */

    tokenBuffer = strndup(inputBuffer + tokenBeginIndex, tokenLength);

    return tokenBuffer;
}

void FreeInputTokens(char** inputTokens, int numOfTokens)
{
    size_t i;

    for (i = 0; i < numOfTokens; ++i)
        free(inputTokens[i]);

    free(inputTokens);
}

enum TokenType GetTokenType(const char* tokenStr)
{
    if (strcmp(tokenStr, ">") == 0)
        return TokenTypeGreat;
    else if (strcmp(tokenStr, "<") == 0)
        return TokenTypeLess;
    else if (strcmp(tokenStr, ">>") == 0)
        return TokenTypeGreatGreat;
    else if (strcmp(tokenStr, ">&") == 0)
        return TokenTypeGreatAnd;
    else if (strcmp(tokenStr, "|") == 0)
        return TokenTypePipe;
    else if (strcmp(tokenStr, ";") == 0)
        return TokenTypeSemicolon;
    else if (strcmp(tokenStr, "&") == 0)
        return TokenTypeAnd;
    else if (strcmp(tokenStr, "&&") == 0)
        return TokenTypeAndAnd;
    else if (strcmp(tokenStr, "||") == 0)
        return TokenTypeOr;
    else
        return TokenTypeArgument;
}

bool ParseCommand(struct TokenArray* tokenArray, struct CommandInfo* commandInfo)
{
    size_t commandCapacity = INITIAL_TOKEN_CAPACITY;
    size_t numOfSimpleCommands = 0;
    size_t tokenIndex = 0;

    struct SimpleCommandInfo simpleCommand;
    struct RedirectInfo redirectInfo;
    struct SimpleCommandInfo* simpleCommands = NULL;
    struct SimpleCommandInfo* newSimpleCommands = NULL;
    bool isBackgroundExecution = false;

    enum TokenType tokenType;

    memset(&simpleCommand, 0, sizeof(simpleCommand));
    memset(&redirectInfo, 0, sizeof(redirectInfo));
    memset(commandInfo, 0, sizeof(*commandInfo));

    if (tokenArray->Count == 0)
        return true;
    
    simpleCommands = (struct SimpleCommandInfo*)calloc(commandCapacity, sizeof(struct SimpleCommandInfo));

    if (simpleCommands == NULL)
        return false;

    if (!ParseSimpleCommand(tokenArray, &tokenIndex, &simpleCommand, &redirectInfo)) {
        free(simpleCommands);
        return false;
    } else {
        simpleCommands[numOfSimpleCommands] = simpleCommand;
        ++numOfSimpleCommands;
    }

    while (1) {
        if (tokenIndex >= tokenArray->Count)
            break;

        tokenType = GetTokenType(tokenArray->Tokens[tokenIndex]);

        if (tokenType == TokenTypePipe) {
            /* パイプの後に続くコマンドの解析 */
            ++tokenIndex;

            /* パイプの後に続くものがコマンドでなければエラー */
            if (!ParseSimpleCommand(tokenArray, &tokenIndex, &simpleCommand, &redirectInfo)) {
                FreeSimpleCommands(simpleCommands, numOfSimpleCommands);
                return false;
            }

            /* コマンドを追加 */
            simpleCommands[numOfSimpleCommands] = simpleCommand;
            ++numOfSimpleCommands;
        } else if (tokenType == TokenTypeAnd) {
            /* バックグラウンド実行の指示がコマンドの末尾にない場合はエラー */
            if (tokenIndex != tokenArray->Count - 1) {
                FreeSimpleCommands(simpleCommands, numOfSimpleCommands);
                return false;
            }

            isBackgroundExecution = true;
            ++tokenIndex;
        } else {
            FreeSimpleCommands(simpleCommands, numOfSimpleCommands);
            return false;
        }

        if (numOfSimpleCommands >= commandCapacity) {
            commandCapacity += COMMAND_CAPACITY_INCREMENT;
            newSimpleCommands = (struct SimpleCommandInfo*)realloc(
                simpleCommands, sizeof(struct SimpleCommandInfo) * commandCapacity);

            if (newSimpleCommands == NULL) {
                FreeSimpleCommands(simpleCommands, numOfSimpleCommands);
                return false;
            }

            simpleCommands = newSimpleCommands;
        }
    }
    
    commandInfo->SimpleCommands = simpleCommands;
    commandInfo->SimpleCommandCount = numOfSimpleCommands;
    commandInfo->Redirect = redirectInfo;
    commandInfo->IsBackgroundExecution = isBackgroundExecution;

    return true;
}

void FreeSimpleCommands(struct SimpleCommandInfo* simpleCommands, size_t numOfSimpleCommands)
{
    size_t i;

    for (i = 0; i < numOfSimpleCommands; ++i)
        free(simpleCommands[i].Args);

    free(simpleCommands);
}

bool ParseSimpleCommand(
    struct TokenArray* tokenArray, size_t* tokenIndex,
    struct SimpleCommandInfo* simpleCommand, struct RedirectInfo* redirectInfo)
{
    enum TokenType tokenType;

    char** commandArguments = NULL;
    char** newCommandArguments = NULL;
    size_t numOfArguments = 0;
    size_t argumentsCapacity = INITIAL_ARGUMENT_CAPACITY;

    simpleCommand->Argc = 0;
    simpleCommand->Args = NULL;

    if (*tokenIndex >= tokenArray->Count)
        return false;

    commandArguments = (char**)calloc(argumentsCapacity, sizeof(char*));

    if (commandArguments == NULL)
        return false;

    tokenType = GetTokenType(tokenArray->Tokens[*tokenIndex]);

    /* 最初がコマンドでない場合はエラー */
    if (tokenType != TokenTypeArgument) {
        free(commandArguments);
        return false;
    } else {
        commandArguments[numOfArguments] = tokenArray->Tokens[*tokenIndex];
        ++numOfArguments;
        ++(*tokenIndex);
    }

    while (1) {
        if (*tokenIndex >= tokenArray->Count)
            break;

        tokenType = GetTokenType(tokenArray->Tokens[*tokenIndex]);

        if (tokenType == TokenTypePipe || tokenType == TokenTypeAnd)
            break;
            
        /* リダイレクトでなければコマンド引数 */
        if (!ParseRedirect(tokenArray, tokenIndex, redirectInfo)) {
            /* リダイレクトまたはコマンド引数でなければエラー */
            if (tokenType != TokenTypeArgument) {
                free(commandArguments);
                return false;
            } else {
                commandArguments[numOfArguments] = tokenArray->Tokens[*tokenIndex];
                ++numOfArguments;
                ++(*tokenIndex);
            }
        }
        
        if (numOfArguments >= argumentsCapacity) {
            argumentsCapacity += ARGUMENT_CAPACITY_INCREMENT;
            newCommandArguments = (char**)realloc(commandArguments, sizeof(char*) * argumentsCapacity);

            if (newCommandArguments == NULL) {
                free(commandArguments);
                return false;
            }

            commandArguments = newCommandArguments;
        }
    }

    commandArguments[numOfArguments] = NULL;

    simpleCommand->Argc = numOfArguments;
    simpleCommand->Args = commandArguments;
    
    return true;
}

bool ParseRedirect(
    struct TokenArray* tokenArray, size_t* tokenIndex,
    struct RedirectInfo* redirectInfo)
{
    enum TokenType redirectTokenType;
    enum TokenType tokenType;

    if (*tokenIndex >= tokenArray->Count)
        return false;

    redirectTokenType = GetTokenType(tokenArray->Tokens[*tokenIndex]);

    if (redirectTokenType != TokenTypeGreat &&
        redirectTokenType != TokenTypeLess &&
        redirectTokenType != TokenTypeGreatGreat &&
        redirectTokenType != TokenTypeGreatAnd)
        return false;

    if (*tokenIndex + 1 >= tokenArray->Count)
        return false;

    tokenType = GetTokenType(tokenArray->Tokens[*tokenIndex + 1]);

    if (tokenType != TokenTypeArgument)
        return false;

    switch (redirectTokenType) {
        case TokenTypeGreat:
            /* 既に標準出力のリダイレクトが指定されている場合は指定しない */
            if (redirectInfo->RedirectOutputFileName == NULL)
                redirectInfo->RedirectOutputFileName = tokenArray->Tokens[*tokenIndex + 1];
            break;
        case TokenTypeLess:
            /* 既に標準入力のリダイレクトが指定されている場合は指定しない */
            if (redirectInfo->RedirectInputFileName == NULL)
                redirectInfo->RedirectInputFileName = tokenArray->Tokens[*tokenIndex + 1];
            break;
        default:
            /* TODO */
            return false;
    }
    
    *tokenIndex += 2;
    
    return true;
}

void ExecuteCommand(struct CommandInfo* commandInfo, bool* isClosed)
{
    size_t i;
    size_t j;
    
    int fdRedirectStdin = -1;
    int fdRedirectStdout = -1;
    int fdRedirectStderr = -1;
    int fdActualStdin = dup(STDIN_FILENO);
    int fdActualStdout = dup(STDOUT_FILENO);
    int fdPipe[2];
    int status;
    pid_t pid;
    pid_t waitPid;

    *isClosed = false;

    if (commandInfo->SimpleCommandCount == 0)
        return;

    if (commandInfo->Redirect.RedirectInputFileName != NULL) {
        fdRedirectStdin = open(commandInfo->Redirect.RedirectInputFileName, O_RDONLY);

        if (fdRedirectStdin == -1) {
            perror("open");
            return;
        }
    } else {
        fdRedirectStdin = dup(fdActualStdin);
    }

    for (i = 0; i < commandInfo->SimpleCommandCount; ++i) {
        dup2(fdRedirectStdin, STDIN_FILENO);
        close(fdRedirectStdin);

        if (i == commandInfo->SimpleCommandCount - 1) {
            if (commandInfo->Redirect.RedirectOutputFileName != NULL) {
                fdRedirectStdout = open(
                    commandInfo->Redirect.RedirectOutputFileName,
                    O_WRONLY | O_CREAT | O_TRUNC, 0644);
                
                if (fdRedirectStdout == -1) {
                    perror("open");
                    return;
                }
            } else {
                fdRedirectStdout = dup(fdActualStdout);
            }
        } else {
            if (pipe(fdPipe) == -1) {
                perror("pipe");
                return;
            }
            fdRedirectStdout = fdPipe[1];
            fdRedirectStdin = fdPipe[0];
        }

        dup2(fdRedirectStdout, STDOUT_FILENO);
        close(fdRedirectStdout);

        for (j = 0; j < NumOfBuiltinCommands; ++j) {
            if (strcmp(BuiltinCommands[j].CommandName,
                       commandInfo->SimpleCommands[i].Args[0]) == 0) {
                (*BuiltinCommands[j].Func)(
                    commandInfo->SimpleCommands[i].Argc,
                    commandInfo->SimpleCommands[i].Args,
                    isClosed);
                break;
            }
        }

        pid = fork();

        if (pid == 0) {
            /* Child process */
            if (j == NumOfBuiltinCommands) {
                execvp(commandInfo->SimpleCommands[i].Args[0], commandInfo->SimpleCommands[i].Args);
                perror("execvp");
                _exit(EXIT_FAILURE);
            } else {
                _exit(EXIT_SUCCESS);
            }
        }
    }

    dup2(fdActualStdin, STDIN_FILENO);
    dup2(fdActualStdout, STDOUT_FILENO);
    close(fdActualStdin);
    close(fdActualStdout);

    if (!commandInfo->IsBackgroundExecution) {
        do {
            waitPid = waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }
}

void BuiltinCd(int argc, char** args, bool* isClosed)
{
    *isClosed = false;

    if (argc != 2) {
        fprintf(stderr, "cd: invalid argument\n");
        return;
    }

    if (chdir(args[1]) == -1)
        perror("chdir");
}

void BuiltinPwd(int argc, char** args, bool* isClosed)
{
    char currentDirectory[PATH_MAX];

    *isClosed = false;

    if (argc != 1) {
        fprintf(stderr, "pwd: invalid argument\n");
        return;
    }

    if (getcwd(currentDirectory, PATH_MAX) != NULL)
        printf("%s\n", currentDirectory);
    else
        perror("getcwd");
}

void BuiltinHelp(int argc, char** args, bool* isClosed)
{
    *isClosed = false;

    if (argc != 1) {
        fprintf(stderr, "help: invalid argument\n");
        return;
    }

    printf("my-shell\n"
           "hello world\n"
           "go fuck yourself\n"
           "you'd rather use bash, tcsh, csh, etc\n");
}

void BuiltinExit(int argc, char** args, bool* isClosed)
{
    if (argc != 1) {
        fprintf(stderr, "exit: invalid argument\n");
        *isClosed = false;
        return;
    }

    printf("Bye-bye\n");

    *isClosed = true;
}



#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main() {
    const char *message = "A B 3";
    FILE *fp;

    fp = fopen("oracle_to_virtual.txt", "w");
    if (fp == NULL) {
        perror("Failed to open message file");
        return 1;
    }

    fprintf(fp, "%s\n", message);
    fclose(fp);
    
    printf("Oracle: Sent configuration: %s\n", message);

    while (1) {
        fp = fopen("virtual_to_oracle.txt", "r");
        if (fp != NULL) {
            char response[256];
            if (fgets(response, sizeof(response), fp) != NULL) {
                response[strcspn(response, "\n")] = '\0';
                printf("Oracle: Received response: %s\n", response);
                fclose(fp);
                break;
            }

            fclose(fp);
        }
        sleep(1);
    }
    return 0;
}

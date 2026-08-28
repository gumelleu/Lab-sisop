#include <stdio.h>
#include <unistd.h>

#define RESET  "\033[0m"
#define VERM   "\033[1;31m"
#define VERDE  "\033[1;32m"
#define AMAR   "\033[1;33m"
#define AZUL   "\033[1;34m"
#define MAGE   "\033[1;35m"
#define CIANO  "\033[1;36m"

int main()
{
    const char *cores[] = { VERM, AMAR, VERDE, CIANO, AZUL, MAGE };
    const char *nome[] = {
        "  ____  _   _  __  __  _____  _      _      _____  _   _ ",
        " / ___|| | | ||  \\/  || ____|| |    | |    | ____|| | | |",
        "| |  _ | | | || |\\/| ||  _|  | |    | |    |  _|  | | | |",
        "| |_| || |_| || |  | || |___ | |___ | |___ | |___ | |_| |",
        " \\____| \\___/ |_|  |_||_____||_____||_____||_____| \\___/ "
    };
    int i;

    printf("\n");

    /* nome desenhando linha por linha */
    for (i = 0; i < 5; i++) {
        printf("%s%s" RESET "\n", cores[i], nome[i]);
        fflush(stdout);
        usleep(90000);
    }

    printf("\n");

    /* barra de carregamento arco-iris */
    printf("  [");
    fflush(stdout);
    for (i = 0; i < 40; i++) {
        printf("%s=" RESET, cores[i % 6]);
        fflush(stdout);
        usleep(20000);
    }
    printf("]\n\n");

    /* piscada final */
    for (i = 0; i < 3; i++) {
        printf(CIANO "  Lab de Sistemas Operacionais - PUCRS" RESET "\r");
        fflush(stdout);
        usleep(220000);
        printf("                                            \r");
        fflush(stdout);
        usleep(140000);
    }

    printf(CIANO "  Lab de Sistemas Operacionais - PUCRS" RESET "\n");
    printf(AMAR  "  Gustavo D Mutti Melleu" RESET "\n\n");

    return 0;
}

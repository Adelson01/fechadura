#include <stdio.h>
#include <string.h> // <-- NOVO: Necessário para a função de comparação de strings (strcmp)
#include "pico/stdlib.h"
#include "hardware/timer.h"

// define o LED de saída
#define GPIO_LED 13

const char senha_correta[5] = "123A"; // <-- MUDE SUA SENHA DE 4 DÍGITOS AQUI
const char senha_correta1[5] = "123B";
const char senha_correta2[5] = "123C";
 int  ctrl= 0;

// define os pinos do teclado com as suas portas GPIO
uint columns[4] = {16, 9, 8, 4};   // Pinos para as colunas
uint rows[4] = {20, 19, 18, 17}; // Pinos para as linhas

//mapa de teclas
char KEY_MAP[16] = {
    '1', '2' , '3', 'A',
    '4', '5' , '6', 'B',
    '7', '8' , '9', 'C',
    '*', '0' , '#', 'D'
};

// As funções do keypad (imprimir_binario, pico_keypad_init, pico_keypad_get_key)
// permanecem exatamente as mesmas.

uint _columns[4];
uint _rows[4];
char _matrix_values[16];
uint all_columns_mask = 0x0;
uint column_mask[4];

void imprimir_binario(int num) {
    int i;
    for (i = 31; i >= 0; i--) {
        (num & (1 << i)) ? printf("1") : printf("0");
    }
}

void pico_keypad_init(uint columns[4], uint rows[4], char matrix_values[16]) {

    for (int i = 0; i < 16; i++) {
        _matrix_values[i] = matrix_values[i];
    }

    for (int i = 0; i < 4; i++) {

        _columns[i] = columns[i];
        _rows[i] = rows[i];

        gpio_init(_columns[i]);
        gpio_init(_rows[i]);

        gpio_set_dir(_columns[i], GPIO_IN);
        gpio_set_dir(_rows[i], GPIO_OUT);

        gpio_put(_rows[i], 1);

        all_columns_mask = all_columns_mask + (1 << _columns[i]);
        column_mask[i] = 1 << _columns[i];
    }
}

char pico_keypad_get_key(void) {
    int row;
    uint32_t cols;
    bool pressed = false;

    cols = gpio_get_all();
    cols = cols & all_columns_mask;
    
    if (cols == 0x0) {
        return 0;
    }

    for (int j = 0; j < 4; j++) {
        gpio_put(_rows[j], 0);
    }

    for (row = 0; row < 4; row++) {

        gpio_put(_rows[row], 1);

        busy_wait_us(100); 

        cols = gpio_get_all();
        gpio_put(_rows[row], 0);
        cols = cols & all_columns_mask;
        if (cols != 0x0) {
            break;
        }   
    }

    for (int i = 0; i < 4; i++) {
        gpio_put(_rows[i], 1);
    }

    if (cols == column_mask[0]) {
        return (char)_matrix_values[row * 4 + 0];
    }
    else if (cols == column_mask[1]) {
        return (char)_matrix_values[row * 4 + 1];
    }
    if (cols == column_mask[2]) {
        return (char)_matrix_values[row * 4 + 2];
    }
    else if (cols == column_mask[3]) {
        return (char)_matrix_values[row * 4 + 3];
    }
    else {
        return 0;
    }
}


void teste_senha(char senha_digitada[5], const char senha_correta[5]){
 if (strcmp(senha_digitada, senha_correta) == 0) {
                    printf("BEM VINDA DANI.\n");
                  ctrl=1;
                } 
                 else if (strcmp(senha_digitada, senha_correta1) == 0) {
                    printf("BEM VINDO GUILHERME.\n");
                   ctrl = 1;
                } else if (strcmp(senha_digitada, senha_correta2) == 0) {
                    printf("BEM VINDO ADELSON.\n");
                   ctrl = 1;
                } else {
                    printf("Senha incorreta. Tente novamente.\n");
                    gpio_put(GPIO_LED, false); // Mantém ou desliga o LED
                }
}


// --- FUNÇÃO PRINCIPAL MODIFICADA PARA A LÓGICA DE SENHA ---
int main() {
    stdio_init_all();
    pico_keypad_init(columns, rows, KEY_MAP);
    
    gpio_init(GPIO_LED);
    gpio_set_dir(GPIO_LED, GPIO_OUT);
    gpio_put(GPIO_LED, false); // Garante que o LED comece desligado

    // Variáveis para a lógica da senha
   
    char senha_digitada[5];
    int indice_senha = 0;

    printf("Digite a senha de 4 digitos:\n");

    while (true) {
        char caracter_press = pico_keypad_get_key();

        // Só processa se uma tecla foi realmente pressionada
        if (caracter_press != 0) {
            
            // Adiciona o caractere à senha digitada se ainda houver espaço
            if (indice_senha < 4) {
                senha_digitada[indice_senha] = caracter_press;
                indice_senha++;
              printf("%c", caracter_press);// Fornece um feedback visual que a tecla foi recebida
            }

            // Se 4 dígitos foram inseridos, verifica a senha
            if (indice_senha == 4) {
                senha_digitada[4] = '\0'; // Adiciona o terminador nulo para formar uma string válida

                printf("\nVerificando...\n");

                // Compara a senha digitada com a senha correta
               teste_senha(senha_digitada, senha_correta);
             if(ctrl == 1){
                               gpio_put(GPIO_LED, true); // Acende o LED
                                ctrl = 0;
             }
                // Reseta o índice para a próxima tentativa
                indice_senha = 0;
                printf("\nDigite a senha de 4 digitos:\n");
            }
        }
        
        // Pequeno delay para evitar leituras múltiplas da mesma tecla
        busy_wait_us(200000); 
    }
}
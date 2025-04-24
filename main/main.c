#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <semphr.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/uart.h"
#include <stdio.h>

// Definições para o ADS1015
#define I2C_PORT i2c1
#define ADS1015_ADDRESS 0x48 
#define ADS1015_CONVERSION_REG 0x00
#define ADS1015_CONFIG_REG 0x01
#define BAUD_RATE 100000

QueueHandle_t xQueueADC;
SemaphoreHandle_t xSemaphore_btn;

int BOTAO = 16;
int SDA = 14;
int SCL = 15;

typedef struct adc {
    int axis; 
    int val;  
} adc_t;

void i2c_init_ads1015() {
    i2c_init(I2C_PORT, BAUD_RATE);
    gpio_set_function(SDA, GPIO_FUNC_I2C);
    gpio_set_function(SCL, GPIO_FUNC_I2C);
    gpio_pull_up(SDA);
    gpio_pull_up(SCL);
}

void ads1015_configure(uint16_t config) {
    uint8_t data[3];
    data[0] = ADS1015_CONFIG_REG;
    data[1] = (config >> 8) & 0xFF;
    data[2] = config & 0xFF; 
    i2c_write_blocking(I2C_PORT, ADS1015_ADDRESS, data, 3, false);
}

int16_t ads1015_read() {
    uint8_t reg = ADS1015_CONVERSION_REG;
    uint8_t data[2];
    i2c_write_blocking(I2C_PORT, ADS1015_ADDRESS, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, ADS1015_ADDRESS, data, 2, false);
    return (int16_t)((data[0] << 8) | data[1]);
}

void flex_sensor_task(void *p) {
    i2c_init_ads1015(); 
    int low;
    int prev=1;
    while (1) {
        uint16_t config = 0;
        config |= 0x8000; 
        config |= 0x4000; 
        config |= 0x0200;
        config |= 0x0100;
        config |= 0x0080;
        config |= 0x0003;

        ads1015_configure(config);

        vTaskDelay(pdMS_TO_TICKS(15));

        int16_t output = ads1015_read();
        output = output/100;
        if(output<190){
            low=0;
        }
        else{
            low=1;
        }
        adc_t data = {3, low};
        if (low != prev){
            //printf("Flex: %d\n", output);
            xQueueSend(xQueueADC, &data, pdMS_TO_TICKS(50));
        }
        prev = low;
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}
void btn_callback(uint gpio, uint32_t events) {
    if (events == 0x04) { 
        if (gpio == BOTAO) {
            xSemaphoreGiveFromISR(xSemaphore_btn, NULL);
        }
    } else if (events == 0x08) {
        xSemaphoreTakeFromISR(xSemaphore_btn, NULL);
    }
}


int filtro_movimento(int new_value, int *buffer, int index) {
    int sum = 0;
    for (int i = 0; i < 5; i++) {
        sum += buffer[i];
    }
    sum -= buffer[index];
    buffer[index] = new_value;
    sum += new_value;
    return sum / 5;
}

void x_task(void *p) {
    adc_init(); 
    int prev_value = 0;
    int buffer[5] = {2047, 2047, 2047, 2047, 2047};
    int x_index = 0;

    while (1) {
        adc_select_input(0); 
        uint16_t read = adc_read(); 
        int movimento_filtrado = filtro_movimento(read, buffer, x_index);
        x_index++;
        if (x_index > 4) {
            x_index = 0;
        }
        int scaled = (movimento_filtrado - 2047)/8; 
        if (scaled < 30 && scaled > -30) {
            scaled = 0;
        }

        adc_t data = {0, scaled};
        if (prev_value == 0) {
            xQueueSend(xQueueADC, &data, pdMS_TO_TICKS(50)); 
        }
        if (scaled == 0) {
            prev_value = 1;
        }
        else {
            prev_value = 0;
        }    
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

void y_task(void *p) {
    adc_init(); 
    int prev_value = 0;
    int buffer[5] = {2047, 2047, 2047, 2047, 2047};
    int y_index = 0;

    while (1) {
        adc_select_input(1); 
        uint16_t read = adc_read(); 
        int movimento_filtrado = filtro_movimento(read, buffer, y_index);
        y_index++;
        if (y_index > 4) {
            y_index = 0;
        }
        int scaled = (movimento_filtrado - 2047)/8; 
        scaled = -scaled;
        if (scaled < 30 && scaled > -30) {
            scaled = 0;
        }
        adc_t data = {1, scaled};
        if (prev_value == 0) {
            xQueueSend(xQueueADC, &data, pdMS_TO_TICKS(50)); 
        }
        if (scaled == 0) {
            prev_value = 1;
        }
        else {
            prev_value = 0;
        }    
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

void button_task(void *p) {
    while (1) {
        if (xSemaphoreTake(xSemaphore_btn, 0) == pdTRUE) {
            adc_t data = {2, 0}; 
            xQueueSend(xQueueADC, &data, pdMS_TO_TICKS(50)); 
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void uart_task(void *p) {
    while (1) {
        adc_t data;
        if (xQueueReceive(xQueueADC, &data, portMAX_DELAY)) { 
            putchar_raw(data.axis);         
            putchar_raw(data.val & 0xFF);          
            putchar_raw((data.val >> 8) & 0xFF);   
            putchar_raw(0xFF);  
        }
    }
}

int main() {
    stdio_init_all();
    adc_init();
    adc_gpio_init(26);
    adc_gpio_init(27);

    gpio_init(BOTAO);
    gpio_set_dir(BOTAO, GPIO_IN);
    gpio_pull_up(BOTAO);

    xQueueADC = xQueueCreate(10, sizeof(adc_t));
    xSemaphore_btn = xSemaphoreCreateBinary(); 

    gpio_set_irq_enabled_with_callback(BOTAO, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &btn_callback);

    xTaskCreate(x_task, "X Task", 4096, NULL, 1, NULL);
    xTaskCreate(y_task, "Y Task", 4096, NULL, 1, NULL);
    xTaskCreate(button_task, "Button Task", 4096, NULL, 1, NULL);
    xTaskCreate(flex_sensor_task, "Flex Sensor Task", 4096, NULL, 1, NULL);
    xTaskCreate(uart_task, "UART Task", 4096, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true);
}
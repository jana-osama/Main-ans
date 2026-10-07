#define F_CPU 16000000UL

#include <avr/io.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <util/delay.h>

#include "I2C.h"
#include "LCD.h"
#include "LM75.h"
#include "EEPROM.h"
#include "UART.h"
#include "SPI.h"
#include "CLOCK.h"

#define HEATER_PIN          PB0
#define FAN_PIN             PB1
#define WARNING_LED_PIN     PD4
#define BUZZER_PIN          PD5

#define EEPROM_MIN_ADDR     0x0000
#define EEPROM_MAX_ADDR     0x0001
#define EEPROM_MAGIC_ADDR   0x0002
#define EEPROM_MAGIC        0xA5

#define DEFAULT_MIN_TEMP    18
#define DEFAULT_MAX_TEMP    28
#define HYSTERESIS          1
#define HIGH_TEMP           35
#define NIGHT_SHIFT         2

#define SPI_START           0xAA
#define SPI_WAIT_START      0
#define SPI_READ_DIP        1
#define SPI_READ_B1         2
#define SPI_READ_B2         3

uint8_t min_temp;
uint8_t max_temp;
uint8_t sensor_error_count = 0;
uint8_t high_temp_warning = 0;

uint8_t panel_dip = 0;
uint8_t panel_b1 = 1;
uint8_t panel_b2 = 1;
uint8_t spi_state = SPI_WAIT_START;

void Load_Settings(void)
{
    uint8_t magic = EEPROM_Read(EEPROM_MAGIC_ADDR);

    if (magic != EEPROM_MAGIC)
    {
        min_temp = DEFAULT_MIN_TEMP;
        max_temp = DEFAULT_MAX_TEMP;

        EEPROM_Write(EEPROM_MIN_ADDR, min_temp);
        EEPROM_Write(EEPROM_MAX_ADDR, max_temp);
        EEPROM_Write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
    }
    else
    {
        min_temp = EEPROM_Read(EEPROM_MIN_ADDR);
        max_temp = EEPROM_Read(EEPROM_MAX_ADDR);
    }
}

void Greenhouse_Control(uint8_t temperature)
{
    uint8_t current_min = min_temp;
    uint8_t current_max = max_temp;

    if (Clock_IsNight())
    {
        current_min = min_temp - NIGHT_SHIFT;
        current_max = max_temp - NIGHT_SHIFT;
    }

    if (temperature < current_min)
        SET_BIT(PORTB, HEATER_PIN);
    else if (temperature >= current_min + HYSTERESIS)
        CLEAR_BIT(PORTB, HEATER_PIN);

    if (temperature > current_max)
        SET_BIT(PORTB, FAN_PIN);
    else if (temperature <= current_max - HYSTERESIS)
        CLEAR_BIT(PORTB, FAN_PIN);
}

uint8_t Read_Temperature(uint8_t *temperature)
{
    int8_t temp = LM75_ReadTemperature();

    if (temp == -1)
    {
        sensor_error_count++;
        return 0;
    }

    sensor_error_count = 0;
    *temperature = (uint8_t)temp;

    return 1;
}

void High_Temperature_Warning(uint8_t temperature)
{
    if (temperature > HIGH_TEMP)
    {
        if (high_temp_warning == 0)
        {
            SET_BIT(PORTD, BUZZER_PIN);
            _delay_ms(300);
            CLEAR_BIT(PORTD, BUZZER_PIN);
            high_temp_warning = 1;
        }
    }
    else
    {
        high_temp_warning = 0;
    }
}

void Parse_Command(char *data)
{
    uint8_t value;

    if (strncmp(data, "SETMIN", 6) == 0)
    {
        value = atoi(data + 7);

        if (value >= 15 && value <= 30)
        {
            min_temp = value;
            EEPROM_Write(EEPROM_MIN_ADDR, min_temp);
        }
    }

    if (strncmp(data, "SETMAX", 6) == 0)
    {
        value = atoi(data + 7);

        if (value >= 15 && value <= 30)
        {
            max_temp = value;
            EEPROM_Write(EEPROM_MAX_ADDR, max_temp);
        }
    }
}

void SPI_Receive_Panel(void)
{
    uint8_t data;

    if (GET_BIT(PINB, PB2) == 0)
    {
        data = SPI_Receive();

        if (spi_state == SPI_WAIT_START)
        {
            if (data == SPI_START)
                spi_state = SPI_READ_DIP;
        }
        else if (spi_state == SPI_READ_DIP)
        {
            panel_dip = data;
            spi_state = SPI_READ_B1;
        }
        else if (spi_state == SPI_READ_B1)
        {
            panel_b1 = data;
            spi_state = SPI_READ_B2;
        }
        else if (spi_state == SPI_READ_B2)
        {
            panel_b2 = data;
            spi_state = SPI_WAIT_START;
        }
    }
}

void Display_Temperature(uint8_t temperature)
{
    LCD_Clear();

    LCD_SendString("TEMP:");
    LCD_SendChar((temperature / 10) + '0');
    LCD_SendChar((temperature % 10) + '0');
    LCD_SendChar('C');

    LCD_SetCursor(1, 0);

    LCD_SendString("F:");

    if (GET_BIT(PORTB, FAN_PIN))
        LCD_SendString("ON ");
    else
        LCD_SendString("OFF");

    LCD_SendString(" H:");

    if (GET_BIT(PORTB, HEATER_PIN))
        LCD_SendString("ON ");
    else
        LCD_SendString("OFF");
}

int main(void)
{
    uint8_t temperature;
    char command[20];
    uint8_t index = 0;
    char received_char;

    SET_BIT(DDRB, HEATER_PIN);
    SET_BIT(DDRB, FAN_PIN);
    SET_BIT(DDRD, WARNING_LED_PIN);
    SET_BIT(DDRD, BUZZER_PIN);

    CLEAR_BIT(PORTB, HEATER_PIN);
    CLEAR_BIT(PORTB, FAN_PIN);
    CLEAR_BIT(PORTD, WARNING_LED_PIN);
    CLEAR_BIT(PORTD, BUZZER_PIN);

    I2C_MasterInit();
    LCD_Init();
    LM75_Init();
    UART_Init(9600);
    SPI_SlaveInit();
    Clock_Init();

    Load_Settings();

    while (1)
    {
        SPI_Receive_Panel();

        if (Read_Temperature(&temperature))
        {
            CLEAR_BIT(PORTD, WARNING_LED_PIN);
            Greenhouse_Control(temperature);
            High_Temperature_Warning(temperature);
            Display_Temperature(temperature);
        }
        else if (sensor_error_count >= 3)
        {
            SET_BIT(PORTD, WARNING_LED_PIN);
            CLEAR_BIT(PORTB, HEATER_PIN);
            CLEAR_BIT(PORTB, FAN_PIN);

            LCD_Clear();
            LCD_SendString("Sensor Fault");
        }

        if (UART_DataAvailable())
        {
            received_char = UART_ReceiveChar();

            if (received_char == '\n')
            {
                command[index] = '\0';
                Parse_Command(command);
                index = 0;
            }
            else if (index < 19)
            {
                command[index] = received_char;
                index++;
            }
        }

        Clock_Tick();
        _delay_ms(1000);
    }

    return 0;
}

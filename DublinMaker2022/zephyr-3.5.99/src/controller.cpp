#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include "controller.h" 
// Button configuration
#define UP_PORT_BIT 28
#define DOWN_PORT_BIT 03
#define LEFT_PORT_BIT 25 
#define RIGHT_PORT_BIT 5 
#define A_PORT_BIT 1
#define B_PORT_BIT 30
static const struct device *gpio0,*gpio1;

int controller::begin()
{
	gpio0=device_get_binding("GPIO_0");
	gpio1=device_get_binding("GPIO_1");
	if (gpio0 == NULL)
	{
		printk("Error acquiring GPIO 0 interface\n");
		return -1;
	}
	if (gpio1 == NULL)
	{
		printk("Error acquiring GPIO 1 interface\n");
		return -2;
	}
	gpio_pin_configure(gpio0, UP_PORT_BIT, GPIO_INPUT | GPIO_PULL_UP);
	gpio_pin_configure(gpio0, DOWN_PORT_BIT, GPIO_INPUT | GPIO_PULL_UP);
	gpio_pin_configure(gpio0, LEFT_PORT_BIT, GPIO_INPUT | GPIO_PULL_UP);
	gpio_pin_configure(gpio1, RIGHT_PORT_BIT, GPIO_INPUT | GPIO_PULL_UP);
	gpio_pin_configure(gpio0, A_PORT_BIT, GPIO_INPUT | GPIO_PULL_UP);
	gpio_pin_configure(gpio0, B_PORT_BIT, GPIO_INPUT | GPIO_PULL_UP);
	return 0;
}

void controller::stopADC()
{
}
uint16_t controller::getButtonState()
{
    uint16_t ReturnValue = 0;
	uint32_t port_state0 = 0;
	uint32_t port_state1 = 0;
	ReturnValue = gpio_port_get_raw(gpio0,&port_state0);
	ReturnValue = gpio_port_get_raw(gpio1,&port_state1);		
	if ((port_state0 & (1 << UP_PORT_BIT))==0)
		ReturnValue |= Up;
	if ((port_state0 & (1 << DOWN_PORT_BIT))==0)
		ReturnValue |= Down;
	if ((port_state0 & (1 << LEFT_PORT_BIT))==0)
		ReturnValue |= Left;
	if ((port_state1 & (1 << RIGHT_PORT_BIT))==0)
			ReturnValue |= Right;
	if ((port_state0 & (1 << A_PORT_BIT))==0)
			ReturnValue |= A;
	if ((port_state0 & (1 << B_PORT_BIT))==0)	
			ReturnValue |= B;		
	
    return ReturnValue;
}
uint16_t controller::readTemperature()
{
	return 0;
}

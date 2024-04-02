# Matrix-310 Modbus TCP Gateway

## Description

This project is a Modbus RTU to TCP gateway. It allows communication between Modbus RTU devices and Modbus TCP clients.

## Table of Contents

- [Installation](#installation)
- [Setting](#Setting)
- [Usage](#usage)

## Installation

To install the Matrix-310 Modbus TCP Gateway, follow these steps:

1. Download the Flash Download Tools from [Espressif](https://www.espressif.com/en/support/download/other-tools).

2. Refer to the [partition table](compile/partitions.csv) file for the correct configuration.

3. Burn the [binary file](build) to Matrix-310.

## Setting

To configure the Matrix-310 Modbus TCP Gateway, follow these steps:

1. Login to the gateway's web interface.
    ![Login](img/login.png)

2. Navigate to the network settings section and configure the network parameters according to your requirements.
    ![Network Setting](img/network_setting.png)

3. Configure the serial settings to match the communication parameters of your Modbus RTU devices.
    ![Serial Setting](img/serial_setting.png)

4. Set up the gateway settings to define the mapping between Modbus RTU devices and Modbus TCP clients.
    ![Gateway Setting](img/gateway_setting.png)

5. Access the system information to view details about the Matrix-310 device.
    ![System Info](img/system_info.png)

## Usage

To use the Matrix-310 Modbus TCP Gateway, follow these steps:

1. Connect your Modbus RTU devices to the Matrix-310 device.

2. Start the Matrix-310 device.

3. Open a Modbus TCP client application on your computer.

4. Enter the IP address and port number of the Matrix-310 device in the client application.

5. Use the client application to send Modbus TCP requests to the Matrix-310 device.

6. Monitor the responses from the Modbus RTU devices in the client application.
![Read Data](img/read_data.png)
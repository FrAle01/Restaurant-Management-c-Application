## 🍽️ Restaurant Client-Server Application

This project was developed as part of the **Computer Networks 2022/23** course and utilizes the **Client-Server** paradigm in **C** with the **TCP** protocol to manage table reservations and orders within a restaurant. The application is designed for a distributed environment and includes three main components:

- **Client**: Manages reservations.
- **Table Device**: Handles orders from tables.
- **Kitchen Device**: Manages kitchen orders.

## 🖥️ Overview

The project uses the Client-Server paradigm and the TCP protocol to ensure reliable connections between devices. Connections are managed using **I/O Multiplexing** on the server side, allowing efficient handling of multiple clients. Data related to connections and orders is stored in files to ensure persistence.

The application supports both **text** and **binary** communication protocols:  
- **Text** is primarily used for verbose message exchanges.
- **Binary** is used to easily transmit numeric values.

---

## 🧩 Components

### Client (`cli.c`)
The client is responsible for making reservations and sends commands such as:

- **find**: Sends a search request for available tables.
- **book**: Reserves a table based on availability.
- **esc**: Closes the connection and exits the process.

### Table Device (`td.c`)
The table device interacts with the server to manage orders from a specific table. Key commands include:

- **login**: Authenticates the table device using a reservation code.
- **menu**: Displays the restaurant menu.
- **order**: Submits an order to the server and receives confirmation.
- **bill**: Calculates and displays the total bill for all orders made.

### Kitchen Device (`kd.c`)
The kitchen device listens for incoming orders from the server and processes them accordingly. Key commands include:

- **take**: Accepts a pending order.
- **show**: Displays orders in preparation.
- **ready**: Marks an order as ready for service.

### Server (`server.c`)
The server manages the connections and processes commands from clients, table devices, and kitchen devices. Key functionalities include:

- **stat**: Displays the status of orders.
- **stop**: Shuts down all connections once all orders are served.

---

## 📂 Files Utility

Several key files are used to manage the application’s functionality:

- **devices.txt**: Maps connected clients to their socket descriptors.
- **reservation.txt**: Stores reservation data.
- **ordini$$$$.txt**: Handles table-specific order data, where `$$$$` represents the reservation code.




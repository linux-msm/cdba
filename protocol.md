# CDBA protocol description

## General stuff:

### cdba operates over ssh

### Client:
* `~/.ssh/config` is effectively the 'real' config on the client side
* The connection is established by executing:
```sh
# serverbinaryname defaults to 'cdba-server'
/usr/bin/ssh user@server serverbinaryname
```
* ssh `stdout` receives `messages` and the client is expected to handle them instantly after receiving, until `EOF`
* ssh `stderr` receives `status updates` (blue text) and the client is expected to handle them instantly after receiving, until `EOF`

### Server:
* The server enumerates all of the connected devices, based on a configuration file
* Client messages are received over `stdin`
* Messages are sent to the clients over `stdout`, until the connection is closed (or drops)
* The server opens a tty to the serial port on the device and forwards the output via `MSG_CONSOLE`

## Supported messages
`C - client | S - server`


### 1 - MSG_SELECT_BOARD [C -> S]
> ```data = string board name | len = sizeof(data) ```
>
> Select the board to operate on.
>
> Return [**S**]: Echo the command back with `len = 0`.

### 2 - MSG_CONSOLE [C <-> S]
> ```data = string_buffer | len = sizeof(data) ```
> Send TTY I/O.
>
> Return [**C**/**S**]: Echo the command back with `len = sizeof(buf), data = buf`.

### 3 - MSG_HARDRESET
`<unused>`

### 4 - MSG_POWER_ON [C -> S]
> ```len = 0```
>
> Power on the board.
>
> Return [**S**]: Echo the command back with `len = 0`.

### 5 - MSG_POWER_OFF [C -> S]
> ```len = 0```
>
> Power off the board.
>
> Return [**S**]: Echo the command back with `len = 0`.

### 6 - MSG_FASTBOOT_PRESENT [S -> C]
> ```data = 0 / 1 | len = 1 ```
>
> Signal that the device is available over fastboot and ready to receive further commands.
> Return [**C**]: MSG_FASTBOOT_DOWNLOAD or MSG_FASTBOOT_CONTINUE with `len = 0`.

### 7 - MSG_FASTBOOT_DOWNLOAD [C -> S]
> ```data = binary_chunk | len = sizeof(data)```
>
> Send binary data for fastboot.
>
> Return [**S**]: Echo the command back with `len = 0`.

### 8 - MSG_FASTBOOT_BOOT
`<unused>`

### 9 - MSG_STATUS_UPDATE [C -> S]
> TODO: json
>
>Return:

### 10 - MSG_VBUS_ON [C -> S]
> Enable USB VBUS going to the board.
>
> Return [**S**]: None

### 11 - MSG_VBUS_OFF [C -> S]
> Disable USB VBUS going to the board.
>
> Return [**S**]: None

### 12 - MSG_FASTBOOT_REBOOT
`<unused>`

### 13 - MSG_SEND_BREAK [C -> S]
> ```len = 0```
>
> Send a tcsendbreak to the board.
>
> Return [**S**]: None

### 14 - MSG_LIST_DEVICES [C - > S]
> ```len = 0```
>
> List the available boards.
>
> Return [**S**]: Echo the command back NUM_DEVICES times with `len = sizeof(buf), data = buf`. Once done, echo the command back with `len = 0`.

### 15 - MSG_BOARD_INFO [C -> S]
> ```data = string board name | len = sizeof(data) ```
>
> Get the board description.
>
> Return [**S**]: Echo the command back with `len = sizeof(buf), data = buf`.

### 16 - MSG_FASTBOOT_CONTINUE [C -> S]
> ```len = 0```
>
> Send `fastboot continue` to the board.
>
> Return [**S**]: Echo the command back with `len = 0`.

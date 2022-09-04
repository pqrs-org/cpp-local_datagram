# example

## Expected result

```shell
make all
make run
```

```text
./build/example
server bound
client connected
processed `1`
server received size:1
buffer: `1`
server received size:2
buffer: `12`
server received size:30720
buffer: `33333333333333333333333333333333333333333... (30720bytes)`
client received size:1
buffer: `1`
server received size:23
buffer: `Type control-c to quit.`
client received size:2
buffer: `12`
client received size:30720
buffer: `33333333333333333333333333333333333333333... (30720bytes)`
client received size:23
buffer: `Type control-c to quit.`
processed `4`
client error_occurred:No buffer space available
```

Note: The order of the lines may change.

all:
	mkdir -p build \
		&& cd build \
		&& cmake .. \
		&& make
	make run

clean:
	rm -rf build

run:
	./build/test
	./build/asio_standalone_test
	./build/asio_test

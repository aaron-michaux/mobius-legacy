
mobius: main.cpp
	clang-5.0 -x c++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -Wno-unused-function -Wno-unused-parameter -Os main.cpp -lstdc++ -lstdc++fs -o mobius

install: mobius
	sudo cp mobius /usr/local/bin

clean:
	rm -f mobius


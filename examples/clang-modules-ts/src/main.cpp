
import std.core;
import stuff.example;

using namespace stuff;

int main(int, char**)
{
   observer_ptr<const char> name("Jeeves");
   say_hello(name.get(), std::cout);
   return 0;
}

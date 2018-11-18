
import std.core;
import stuff.example;

using namespace stuff;

int main(int argc, char** argv)
{
   const char* name = (argc > 1) ? argv[1] : "Jeeves";
   say_hello(name, std::cout);
   return 0;
}

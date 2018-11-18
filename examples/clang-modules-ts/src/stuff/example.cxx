
module stuff.example;

import std.core;

namespace stuff
{
void say_hello(std::string_view person, std::ostream& out)
{
   out << "Hello " << person << "!" << std::endl;
}

} // namespace stuff

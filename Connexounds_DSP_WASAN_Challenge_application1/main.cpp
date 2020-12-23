/*
    TODO:
        I.------enforce conventions across entire project.
*/

#include "Aggregator.h"
#include <iostream>
#include "lib/cli/cli.h"
#include "lib/cli/clifilesession.h"

using namespace cli;
using namespace std;

/// <summary>
/// <para>Entry point of the program.</para>
/// </summary>
/// <param name="argc"></param>
/// <param name="argv"></param>
/// <returns></returns>
int main(int argc, char* argv[])
{
    // ASCII art
    std::cout
        << "                           " GREEN("__   __") "                     _     "         << std::endl
        << "                           " GREEN("\\ \\ / /") "                    | |    "       << std::endl
        << "  ___ ___  _ __  _ __   ___ " GREEN("\\ V /") " ___  _   _ _ __   __| |___ "        << std::endl
        << " / __/ _ \\| '_ \\| '_ \\ / _ \\ " GREEN("> <") " / _ \\| | | | '_ \\ / _` / __|"   << std::endl
        << "| (_| (_) | | | | | | |  __/" GREEN("/ . \\") " (_) | |_| | | | | (_| \\__ \\"      << std::endl
        << " \\___\\___/|_| |_|_| |_|\\___" GREEN("/_/ \\_\\") "___/ \\__,_|_| |_|\\__,_|___/"  << std::endl
        << std::endl;

    HRESULT hr = ERROR_SUCCESS;
    Aggregator pAggregator;
    BOOL bQuit = FALSE;

    /////////////////////////////////////////////////
	//////////////// Interactive CLI ////////////////
	/////////////////////////////////////////////////
    auto rootMenu = make_unique< Menu >("cli");
    rootMenu->Insert(
        "hello",
        [&pAggregator](std::ostream& out) { out << "Hello, world\n"; pAggregator.Initialize(); },
        "Print hello world");
    rootMenu->Insert(
        "answer",
        [](std::ostream& out, int x) { out << "The answer is: " << x << "\n"; },
        "Print the answer to Life, the Universe and Everything ");
    rootMenu->Insert(
        "color",
        [](std::ostream& out) { out << "Colors ON\n"; SetColor(); },
        "Enable colors in the cli");
    rootMenu->Insert(
        "nocolor",
        [](std::ostream& out) { out << "Colors OFF\n"; SetNoColor(); },
        "Disable colors in the cli");
    
    auto subMenu = make_unique< Menu >("sub");
    subMenu->Insert(
        "hello",
        [](std::ostream& out) { out << "Hello, submenu world\n"; },
        "Print hello world in the submenu");
    subMenu->Insert(
        "demo",
        [](std::ostream& out) { out << "This is a sample!\n"; },
        "Print a demo string");

    auto subSubMenu = make_unique< Menu >("subsub");
    subSubMenu->Insert(
        "hello",
        [](std::ostream& out) { out << "Hello, subsubmenu world\n"; },
        "Print hello world in the sub-submenu");
    subMenu->Insert(std::move(subSubMenu));

    rootMenu->Insert(std::move(subMenu));

    Cli cli(std::move(rootMenu));
    // global exit action
    cli.ExitAction([&pAggregator](auto& out) { out << "Goodbye and thanks for all the fish.\n"; pAggregator.Stop(); });
    
    CliFileSession input(cli);
    input.Start();

	return hr;
}
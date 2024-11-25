#include "include/CLI11.hpp"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using json = nlohmann::json;


// ? Write response data from curl
std::size_t Write_Callback(void *contents, std::size_t size, std::size_t nmemb, std::string *userp)
{
    std::size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}


void Search_Packages(const std::string &search_query, const bool &only_name)
{
    std::string read_buffer;
    CURLcode res;
    CURL *curl;

    curl = curl_easy_init();

    if (curl)
    {
        std::string url = "https://aur.archlinux.org/rpc/?v=5&type=search&arg=" + search_query; 

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Write_Callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) // ? Check for error
        {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << '\n';
            return;
        }

        // ? Parse json response
        auto json_response = json::parse(read_buffer);

        if (json_response["resultcount"] > 0)
        {
            std::cout << "Packages found:\n";

            if (only_name)
            {
                for (const auto &pkg : json_response["results"])
                {
                    std::string pkg_name = pkg.contains("Name") ? pkg["Name"] : "Unknown";
                    std::cout << pkg_name << '\n';
                }

                return;
            }

            for (const auto &pkg : json_response["results"])
            {
                std::string pkg_desc = pkg.contains("Description") ? pkg["Description"] : "No description available";
                std::string pkg_version = pkg.contains("Version") ? pkg["Version"] : "Unknown";
                std::string pkg_name = pkg.contains("Name") ? pkg["Name"] : "Unknown";

                std::cout << pkg_name << ' ' << pkg_version << '\n';
                std::cout << "    " << pkg_desc << '\n';
            }
        }
        else
        {
            std::cout << "No packages found.\n";
        }
    }
}


std::string Home()
{
    return std::getenv("HOME");
}


std::string Install_Path()
{
    return Home() + "/.cache/hone/";
}


std::string List_Path()
{
    return Install_Path() + "Installed/";
}


bool Does_Install_Dir_Exists()
{
    return std::filesystem::exists(Install_Path()) && std::filesystem::is_directory(Install_Path());
}



bool Does_Package_List_Exists()
{
    return std::filesystem::exists(List_Path()) && std::filesystem::is_directory(List_Path());
}


/*
    * @param add if true, then the function will switch to add mode, if false, the function will only list package
*/
bool List_Package(const bool &add = false, const std::string &added_package = "")
{
    if (!Does_Package_List_Exists())
    {
        std::filesystem::create_directory(List_Path());
        std::filesystem::current_path(List_Path());
        std::ofstream list("package_list.txt");
        list.close();
    }

    if (add)
    {
        std::ofstream list("package_list.txt", std::ios::app);

        if (!list)
        {
            std::cerr << "Failed to open file!\n";
            return false;
        }

        list << added_package << '\n';

        list.close();

        return true;
    }

    std::ifstream list("package_list.txt");

    if (!list)
    {
        std::cerr << "Failed to open file!\n";
        return false;
    }

    std::string package;

    while (std::getline(list, package))
    {
        std::cout << package << '\n';
    }

    list.close();

    return true;
}


bool Clone_AUR_Package(const std::string &package_query)
{
    if (!Does_Install_Dir_Exists())
    {
        std::filesystem::create_directory(Install_Path());
    }

    std::filesystem::current_path(Install_Path());

    std::string command = "git clone https://aur.archlinux.org/" + package_query + ".git " + Install_Path() + package_query;
    bool result = !std::system(command.c_str());

    if (!result)
    {
        std::cerr << "Failed to clone AUR package?\n";
    }

    return result;
}


bool Build_Install_Package(const std::string &package_query)
{
    std::filesystem::path package_path = Install_Path() + package_query;

    if (!std::filesystem::exists(package_path))
    {
        std::cerr << "Package directory does not exist: " << package_path << "\n";
        return false;
    }

    std::filesystem::current_path(package_path);

    bool result = !std::system("makepkg -risc");

    std::filesystem::current_path(Home());

    if (!result)
    {
        std::cerr << "Failed to build package!\n";
    } 
    else
    {
        std::cout << "Successfully build package!\n";
    }

    return result;
}


void Clean(const std::string &package_query)
{
    std::filesystem::current_path(Install_Path());
    std::filesystem::remove_all(package_query);
}


bool Install_AUR_Package(const std::string &package_query)
{
    std::cout << "[INFO]: Cloning package!\n";
    if (!Clone_AUR_Package(package_query))
    {
        return false;
    }

    std::cout << "[INFO]: Building package!\n";
    if (!Build_Install_Package(package_query))
    {
        return false;
    }
    
    List_Package(true, package_query);
    
    return true;
}


int32_t main(int32_t argc, char **argv)
{
    CLI::App app{"AUR Helper Only"};

    std::string install_query;
    std::string search_query;

    bool only_name = false;
    bool is_list = false;

    app.add_option("-d,--download", install_query, "Download packages");

    app.add_option("-s,--search", search_query, "Search for packages");
    app.add_flag("-n,--name", only_name, "Only list pkg's names. Use only with the --search option");
    
    app.add_flag("-l,--list", is_list, "List installed AUR packages");

    CLI11_PARSE(app, argc, argv);

    // ? Restrict the use of multiple arguments
    int8_t option_count = 0;

    if (!install_query.empty()) option_count++;
    if (!search_query.empty()) option_count++;
    if (is_list) option_count++;

    if (option_count > 1)
    {
        std::cerr << "Error: You can only use a single option at a time!\n";
        return 1;
    }

    // ? Checks if --name is being used when --search is not being used
    if (is_list && search_query.empty())
    {
        std::cerr << "Error: Do not use --name outside of --search!\n";
        return 1;
    }

    if (!search_query.empty())
    {
        Search_Packages(search_query, only_name);
        return 0;
    }

    if (!install_query.empty())
    {
        Install_AUR_Package(install_query);
        return 0;
    }

    if (is_list)
    {
        List_Package();
    }

    return 0;
}
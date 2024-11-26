# HONE

Hone is an "AUR Helper" written in C++

## Differences with other helpers

As other helpers implement more features to be different than the others, hone only implement specific features you need, such as:
- Installing package 
- Searching package 
- Updating package 

and not with other helpers such as Yay, that allows you to install from Arch's main repository. Hone only install, and search from the AUR.

This allows hone to be as lightweight as possible.

## Installation

> [!WARNING]
> Only supported option to install right now is with git!

### With git

```sh
sudo pacman -S git curl nlohmann-json
git clone https://github.com/RQuarx/hone
cd hone
./build.sh
cd target
chmod +x hone
```

## Usage

```sh
hone -s --search [package] # Search for packages in the AUR
        -n --name # Search for packages, but only list names
hone -S --Sync [package] # Download package
hone -R --Remove [package] # Removes a package
hone -Q --Query # List downloaded packages
hone -U --update # Updates outdated AUR package
        --no-sysupgrade # Updates AUR without updating system
```
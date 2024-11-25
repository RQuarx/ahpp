# HONE

Hone is an "AUR Helper" written in C++

## Features

- Install package
- Search package
- List installed package

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
hone -s [package] # Search for packages in the AUR
hone -n -s [package] # Search for packages, but only list names
hone -d [package] # Download package
hone -l # List downloaded packages
hone -u # Updates AUR packages that is outdated from the list files
hone -u --no-sysupgrade # Updates AUR without updating system
```

## TODO's

- Add more colours
- Make code more readable
- Removing pkgs


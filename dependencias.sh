#!/bin/bash
if [[ $EUID -ne 0 ]]; then
   echo "Este script debe ejecutarse como root o con sudo." 
   exit 1
fi
echo "Actualizando lista de paquetes..."
apt-get update -y
echo "Instalando dependencias necesarias..."
apt-get install -y build-essential
echo "Instalación completada."
gcc --version && make --version
echo "Compilando el programa con make..."
make
echo "Compilación completada."
#!/usr/bin/env bash
# Пересоздаёт тестовое окружение. Работает ТОЛЬКО внутри директории test.
cd "$(dirname "$0")" || exit 1
chmod -R u+rwx src ro_dst noread_src 2>/dev/null
rm -rf src dst "dst with space" ro_dst noread_src

# исходное дерево
mkdir -p "src/sub1/deep" "src/sub2" "src/папка с пробелами" "src/empty_dir"
echo "root a"          > src/a.txt
echo "sub1 a (dup)"    > src/sub1/a.txt
echo "deep a (dup)"    > src/sub1/deep/a.txt
echo "sub1 b"          > src/sub1/b.txt
echo "sub2 c"          > src/sub2/c.txt
echo "file with space" > "src/папка с пробелами/my file.txt"
echo "hidden"          > src/.hidden
echo "other a (dup)"   > src/sub2/a.txt
ln -s a.txt src/link_to_a.txt           # символическая ссылка (не обычный файл)

# целевые директории
mkdir -p dst "dst with space"

# директория без прав записи
mkdir ro_dst && chmod 555 ro_dst

# исходная директория без прав чтения
mkdir noread_src && echo x > noread_src/x.txt && chmod 000 noread_src

echo "Готово. Структура:"
find . -not -path './setup.sh' | sort

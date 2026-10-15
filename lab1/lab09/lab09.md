# Лабораторная работа 9. Сетевое окружение UNIX

> **Примечание к оформлению.** Выводы команд ниже — типовые для Debian 13. Перед сдачей выполните команды на своей
> машине, сделайте скриншоты и замените ими заглушки `fig/...`, а IP-адрес соседа `192.168.56.102` и имя
> пользователя соседа замените на реальные. Если соседа нет, подключайтесь ко второй своей виртуальной машине или к
> своей же ВМ через проброшенный порт (`ssh -p 2222 vlad@127.0.0.1`).

## Упражнение 9.1. Удаленный доступ в систему с помощью протокола SSH

> **SSH** (Secure Shell) — протокол защищённого удалённого доступа. Весь трафик (включая пароль) шифруется, в отличие
> от устаревших `telnet` и `rsh`. Серверная часть — демон `sshd` (в `ps aux` из лаб. 8 это
> `/usr/sbin/sshd -D`, порт 22), клиентская — команда `ssh`.

### 1. При помощи команды ssh подключитесь к операционной системе соседа

Узнаём IP-адрес соседа (на его машине) и проверяем, что демон работает:

```bash
vlad@debian13:~$ ip -br a
lo               UNKNOWN        127.0.0.1/8 ::1/128
enp0s8           UP             192.168.56.102/24

vlad@debian13:~$ systemctl status ssh | head -3
```

Подключение:

```bash
vlad@debian13:~$ ssh vlad@192.168.56.102
The authenticity of host '192.168.56.102 (192.168.56.102)' can't be established.
ED25519 key fingerprint is SHA256:Xk3...redacted...
This key is not known by any other names.
Are you sure you want to continue connecting (yes/no/[fingerprint])? yes
Warning: Permanently added '192.168.56.102' (ED25519) to the list of known hosts.
vlad@192.168.56.102's password:
Linux debian13 6.12.107+deb13-arm64 #1 SMP Debian 6.12.107-1 aarch64
...
Last login: Mon Oct 12 20:14:03 2026 from 192.168.56.101
vlad@debian13:~$
```

![ssh connect](fig/9-01%20ssh.png)

- `ssh пользователь@хост` — подключение под указанной учётной записью удалённой системы. Без `пользователя@` берётся
  имя текущего пользователя.
- При **первом** подключении клиент показывает отпечаток (fingerprint) ключа сервера и спрашивает подтверждение. Это
  защита от подмены сервера (атака man-in-the-middle). После ответа `yes` ключ сохраняется в `~/.ssh/known_hosts`, и
  при следующих подключениях сверяется автоматически. Если ключ сервера изменится, ssh выдаст предупреждение
  `REMOTE HOST IDENTIFICATION HAS CHANGED`.
- Далее запрашивается пароль удалённой учётной записи (вводимые символы не отображаются).
- Строка `Last login: ... from ...` — время и адрес предыдущего входа в эту учётную запись (то же самое, что мы видели в
  упр. 1.2).
- Приглашение `vlad@debian13:~$` внешне не изменилось (имена машин одинаковые), но команды теперь выполняются на
  **удалённой** системе. Отличить можно по IP в выводе `who`/`w` или командой `hostname -I`.

### 2. Информация об удалённой системе и пользователях

```bash
vlad@debian13:~$ hostname -I
192.168.56.102 10.0.2.15
```

#### a. Имя текущего пользователя

```bash
vlad@debian13:~$ whoami
vlad
```

#### b. UID, GID и идентификаторы вторичных групп

```bash
vlad@debian13:~$ id
uid=1000(vlad) gid=1000(vlad) группы=1000(vlad),24(cdrom),25(floppy),27(sudo),29(audio),30(dip),44(video),46(plugdev),100(users),106(netdev)
```

#### c. Количество и имена пользователей, работающих в системе

```bash
vlad@debian13:~$ users
vlad vlad

vlad@debian13:~$ who
vlad     tty1         2026-10-12 19:50
vlad     pts/0        2026-10-12 20:21 (192.168.56.101)

vlad@debian13:~$ w
 20:25:41 up  1:02,  2 users,  load average: 0,00, 0,01, 0,00
USER     TTY      FROM             LOGIN@   IDLE   JCPU   PCPU  WHAT
vlad     tty1     -                19:50    1:12   0.03s  0.03s -bash
vlad     pts/0    192.168.56.101   20:21    0.00s  0.04s  0.00s w
```

![who and w](fig/9-02%20who%20w.png)

- В `who`/`w` в колонке `FROM` (в скобках в `who`) виден адрес, **с которого** подключился пользователь — так отличается
  сетевой сеанс (`pts/N`, адрес клиента) от локального (`tty1`, прочерк). Наш сеанс — `pts/0` с адреса `192.168.56.101`.
- `users` выводит имя по одному разу на каждый сеанс, поэтому `vlad` повторяется дважды — работают два сеанса, а не два
  разных человека.

#### d. Имя текущего виртуального терминала

```bash
vlad@debian13:~$ tty
/dev/pts/0
```

`pts` (pseudo-terminal slave) — псевдотерминал, создаваемый `sshd` для сетевого сеанса. Для физической/виртуальной
консоли было бы `/dev/tty1`.

#### e. Системная дата и время

```bash
vlad@debian13:~$ date
Пн 12 окт 2026 20:26:12 EEST
```

#### f. Версия операционной системы

```bash
vlad@debian13:~$ uname -a
Linux debian13 6.12.107+deb13-arm64 #1 SMP Debian 6.12.107-1 (2026-08-29) aarch64 GNU/Linux
```

#### g. Время работы системы и нагрузка

```bash
vlad@debian13:~$ uptime
 20:26:40 up  1:03,  2 users,  load average: 0,00, 0,01, 0,00
```

Три числа `load average` — средняя длина очереди готовых к выполнению процессов за 1, 5 и 15 минут.

![remote info](fig/9-02%20remote%20info.png)

### 3. Закончите сеанс работы на машине соседа

```bash
vlad@debian13:~$ exit
logout
Connection to 192.168.56.102 closed.
vlad@debian13:~$
```

Сообщение `Connection to ... closed.` подтверждает, что вернулись в локальную систему.

### 4. Запуск команды на удалённой системе через ssh

```bash
vlad@debian13:~$ ssh vlad@192.168.56.102 ls -la
vlad@192.168.56.102's password:
итого 60
drwx------ 6 vlad vlad 4096 окт 12 20:21 .
drwxr-xr-x 4 root root 4096 сен  7 21:50 ..
-rw------- 1 vlad vlad 2310 окт 12 20:10 .bash_history
-rw-r--r-- 1 vlad vlad  220 сен  7 21:50 .bash_logout
-rw-r--r-- 1 vlad vlad 3526 сен  7 21:50 .bashrc
-rw-r--r-- 1 vlad vlad  807 сен  7 21:50 .profile
-rw-r--r-- 1 vlad vlad   42 сен 20 14:30 .plan
drwxr-xr-x 2 vlad vlad 4096 окт  1 13:40 bin
...
vlad@debian13:~$
```

![ssh remote ls](fig/9-01%20ssh%20ls.png)

Если после `ssh хост` указана команда, `sshd` выполняет её в удалённой системе **без** запуска интерактивной оболочки,
выводит результат на локальный терминал и сразу закрывает соединение. Это удобно для скриптов. Результат —
содержимое домашнего каталога **соседа**, а не локального.

## Упражнение 9.2. Локальная и сетевая почта

> В системе работает MTA (Mail Transfer Agent) `exim4` (виден в `ps aux` как `/usr/sbin/exim4 -bdf -q30m`). Он
> принимает письма от пользовательской программы `mail` и доставляет локальным получателям в почтовый ящик
> `/var/mail/<пользователь>`. Для отправки писем нужен клиент (MUA).

Утилита `mail` по умолчанию не установлена:

```bash
vlad@debian13:~$ mail
-bash: mail: команда не найдена
vlad@debian13:~$ sudo apt install bsd-mailx
```

(`bsd-mailx` предоставляет команды `mail` и `mailx`; в Debian 13 допустима и замена `s-nail`.)

### 1. Отправка нескольких писем пользователю vinnie

```bash
vlad@debian13:~$ mail -s "Test 1" vinnie
Cc:
Hello, Vinnie!
This is the first test letter.
^D
vlad@debian13:~$ mail -s "Lab 9" vinnie
Cc:
Second letter about lab 9.
^D
vlad@debian13:~$ echo "Third letter from pipe" | mail -s "From pipe" vinnie
vlad@debian13:~$
```

![mail send](fig/9-03%20mail%20send.png)

- `-s "тема"` — тема письма, далее адресат (`vinnie` — локальный пользователь; для удалённого —
  `vinnie@host`).
- Тело письма вводится с клавиатуры и завершается `^D` в начале пустой строки (EOF). После `^D` может спрашиваться
  `Cc:` (копия).
- Третий вариант демонстрирует конвейер (лаб. 6): тело письма берётся со stdin.
- При успешной отправке сообщений не выводится («молчание — признак успеха»). Проверить очередь и доставку можно
  командой `sudo mailq` (пусто — всё доставлено) и в журнале `sudo journalctl -u exim4`.

### 2. Прочитайте и удалите письма пользователя vinnie

Переходим в контекст vinnie (лаб. 1, упр. 1.7) или входим на другом терминале:

```bash
vlad@debian13:~$ su - vinnie
Пароль:
У вас есть новая почта.
vinnie@debian13:~$ mail
"/var/mail/vinnie": 3 messages 3 new
>N  1 vlad               Mon Oct 12 20:31  14/419   Test 1
 N  2 vlad               Mon Oct 12 20:31  13/402   Lab 9
 N  3 vlad               Mon Oct 12 20:32  13/405   From pipe
? 1
Message  1:
From vlad@debian13 Mon Oct 12 20:31:07 2026
To: vinnie@debian13
Subject: Test 1

Hello, Vinnie!
This is the first test letter.

? d 1
? d 2-3
? q
vinnie@debian13:~$ mail
No mail for vinnie
vinnie@debian13:~$
```

![mail read](fig/9-03%20mail%20read.png)

Основные команды приглашения `?` программы `mail`:

| Команда | Действие                                                       |
|:--------|:---------------------------------------------------------------|
| `N`     | прочитать письмо с номером N (или просто Enter — следующее)    |
| `h`     | показать список заголовков                                     |
| `d N`   | пометить письмо N к удалению; `d 2-3`, `d *` — несколько / все |
| `r N`   | ответить на письмо                                             |
| `q`     | выйти; помеченные письма удаляются из ящика                    |
| `x`     | выйти **без** внесения изменений (удаления отменяются)         |

В списке: `N` — новое непрочитанное письмо, `>` — текущее. Повторный запуск `mail` выводит `No mail for vinnie` — ящик
пуст. Системное сообщение «У вас есть новая почта» (`You have new mail`) появляется при входе, если в `/var/mail/<user>`
есть непрочитанные письма.

## Упражнение 9.3. Передача файлов с помощью протокола FTP

> **FTP** (File Transfer Protocol) — классический протокол передачи файлов (порт 21 — управляющее соединение, данные
> идут по отдельному соединению). Пароль и данные передаются **открытым текстом**, поэтому сегодня FTP используют
> в основном для публичных архивов с анонимным доступом, а для остального — SCP/SFTP/rsync по SSH.

Клиент `ftp` может быть не установлен:

```bash
vlad@debian13:~$ sudo apt install ftp
```

### 1. Подключение к ftp.yandex.ru (анонимно)

```bash
vlad@debian13:~$ ftp ftp.yandex.ru
Connected to ftp.yandex.ru.
220 Yandex FTP server ready
Name (ftp.yandex.ru:vlad): anonymous
331 Please specify the password.
Password:
230 Login successful.
Remote system type is UNIX.
Using binary mode to transfer files.
ftp>
```

![ftp login](fig/9-04%20ftp%20login.png)

- Код `220` — сервер готов, `331` — нужен пароль, `230` — вход выполнен. Коды ответов FTP трёхзначные: `2xx` —
  успех, `3xx` — нужна дополнительная информация, `4xx/5xx` — ошибка.
- Имя `anonymous` (или `ftp`) — анонимный вход; в качестве пароля по традиции вводят e-mail (допустим любой текст).

> **Если подключение не удаётся.** Многие зеркала отключили FTP в пользу HTTPS/rsync, а каталог `jessie` перенесён в
> архив (`archive.debian.org`), поскольку Debian 8 давно снят с поддержки. В этом случае зафиксируйте сообщение об
> ошибке (например, `ftp: connect: Connection refused` или `550 Failed to change directory`) и повторите упражнение на
> любом доступном анонимном сервере, например `ftp.debian.org` (каталог `/debian/dists/trixie`). Алгоритм команд ниже
> от этого не меняется.

### 2. Переход в каталог /debian/dists/jessie

```bash
ftp> pwd
Remote directory: /
ftp> cd /debian/dists/jessie
250 Directory successfully changed.
ftp> pwd
Remote directory: /debian/dists/jessie
ftp> ls
200 PORT command successful.
150 Here comes the directory listing.
-rw-r--r--    1 ftp      ftp         12345 Jun 06  2020 ChangeLog
drwxr-xr-x    2 ftp      ftp          4096 Jun 06  2020 contrib
-rw-r--r--    1 ftp      ftp         ...   Release
drwxr-xr-x    2 ftp      ftp          4096 Jun 06  2020 main
...
226 Directory send OK.
```

- `cd`, `pwd`, `ls` в `ftp>` работают так же, как в shell, но **на удалённой** стороне. Для локальных операций
  используется `!` (например, `!ls`) и команда `lcd` (смена локального каталога).

### 3. Получение файла ChangeLog

```bash
ftp> lcd ~
Local directory now /home/vlad
ftp> get ChangeLog
local: ChangeLog remote: ChangeLog
200 PORT command successful. Consider using PASV.
150 Opening BINARY mode data connection for ChangeLog (12345 bytes).
226 Transfer complete.
12345 bytes received in 0.12 secs (100.2 kB/s)
```

![ftp get](fig/9-04%20ftp%20get.png)

- `get файл` — скачать файл (удалённый → локальный), `put файл` — отправить (локальный → удалённый), `mget`/`mput` —
  по шаблону, `binary`/`ascii` — режим передачи. Если за NAT не устанавливается передача данных, помогает команда
  `passive` (пассивный режим).
- Код `226 Transfer complete` — передача завершена успешно. Проверка: `ls -l ~/ChangeLog`.

### 4. Завершение сеанса

```bash
ftp> bye
221 Goodbye.
vlad@debian13:~$
```

`bye` (также `quit`, `exit`) закрывает управляющее соединение; сервер отвечает кодом `221`.

### 5. Интерактивные клиенты ncftp, lftp, mc

```bash
vlad@debian13:~$ sudo apt install ncftp lftp
```

**ncftp** — улучшенный клиент: анонимный вход по умолчанию, автодополнение, индикатор прогресса, закладки.

```bash
vlad@debian13:~$ ncftp ftp.yandex.ru
ncftp / > cd /debian/dists/jessie
ncftp /debian/dists/jessie > get ChangeLog
ChangeLog:                 12.06 kB  100.20 kB/s
ncftp /debian/dists/jessie > bye
```

![ncftp](fig/9-04%20ncftp.png)

**lftp** — мощный клиент с автодополнением по Tab, историей, докачкой (`get -c`), параллельными загрузками,
зеркалированием (`mirror`), поддержкой FTP/SFTP/HTTP.

```bash
vlad@debian13:~$ lftp ftp.yandex.ru
lftp ftp.yandex.ru:~> cd /debian/dists/jessie
lftp ftp.yandex.ru:/debian/dists/jessie> get ChangeLog
12345 bytes transferred
lftp ftp.yandex.ru:/debian/dists/jessie> bye
```

![lftp](fig/9-04%20lftp.png)

**Midnight Commander** — FTP-соединение открывается как обычный каталог в панели: `F9` → «Правая панель» →
«FTP-соединение…» (или `Ctrl+\`, либо в командной строке `cd ftp://anonymous@ftp.yandex.ru/debian/dists/jessie`).
Файл копируется клавишей `F5` из удалённой панели в локальную — так же, как между локальными каталогами (упр. 7.3).

![mc ftp](fig/9-04%20mc%20ftp.png)

## Упражнение 9.4. Передача файлов с помощью протокола SCP

> **SCP** (Secure Copy) — копирование файлов поверх SSH. Данные и пароль шифруются, используется тот же порт 22 и та же
> аутентификация, что и у `ssh`. Синтаксис аналогичен `cp`: `scp источник приёмник`, где удалённый путь записывается
> как `пользователь@хост:путь`.

### 1. Копирование .plan соседа в remote.plan

```bash
vlad@debian13:~$ scp vlad@192.168.56.102:.plan remote.plan
vlad@192.168.56.102's password:
.plan                                         100%   42     0.1KB/s   00:00
vlad@debian13:~$
```

![scp](fig/9-05%20scp.png)

- `vlad@192.168.56.102:.plan` — источник: файл `.plan` в домашнем каталоге (относительный путь после `:` отсчитывается
  от домашнего каталога удалённого пользователя; можно писать и `:~/.plan`, и абсолютный `:/home/vlad/.plan`).
- `remote.plan` — приёмник: имя нового локального файла в текущем каталоге.
- Выводится индикатор: имя файла, процент, размер, скорость, время. Направление можно изменить на обратное:
  `scp remote.plan vlad@192.168.56.102:`; для каталогов нужен ключ `-r`.

### 2. Просмотр содержимого remote.plan

```bash
vlad@debian13:~$ cat remote.plan
Студент группы ..., лабораторные работы по UNIX
vlad@debian13:~$ ls -l remote.plan .plan
-rw-r--r-- 1 vlad vlad 42 окт 12 20:50 remote.plan
-rw-r--r-- 1 vlad vlad 31 сен 20 14:30 .plan
```

Содержимое — план **соседа**, отличающийся от локального `.plan`; время создания файла — время копирования.

## Упражнение 9.5. Синхронизация файлов с помощью протокола RSYNC/SSH

> **rsync** копирует только **различия** между источником и приёмником (алгоритм дельта-кодирования), поэтому
> повторная синхронизация большого каталога занимает считанные секунды. В качестве транспорта используется SSH.
> `rsync` должен быть установлен **на обеих** машинах.

```bash
vlad@debian13:~$ sudo apt install rsync
```

### 1. Синхронизация каталога bin соседа в remote.bin

```bash
vlad@debian13:~$ rsync -avz -e ssh vlad@192.168.56.102:bin/ remote.bin/
vlad@192.168.56.102's password:
receiving incremental file list
created directory remote.bin
./
script1.sh
script2.sh

sent 62 bytes  received 354 bytes  92.44 bytes/sec
total size is 512  speedup is 1.23
vlad@debian13:~$
```

![rsync](fig/9-06%20rsync.png)

Разбор команды:

- `-a` (archive) — режим архивирования: рекурсивно, с сохранением прав, владельцев, времени изменения, символических
  ссылок (аналог `-rlptgoD`);
- `-v` (verbose) — подробный вывод списка передаваемых файлов;
- `-z` (compress) — сжатие данных при передаче;
- `-e ssh` — использовать SSH в качестве удалённой оболочки (транспорта);
- `vlad@192.168.56.102:bin/` — источник; **косая черта в конце** означает «содержимое каталога bin», а не сам каталог
  (без неё внутри `remote.bin` появился бы вложенный каталог `bin`);
- `remote.bin/` — приёмник; при отсутствии создаётся (`created directory remote.bin`).

Повторный запуск без изменений ничего не передаёт — это отличие rsync от scp:

```bash
vlad@debian13:~$ rsync -avz -e ssh vlad@192.168.56.102:bin/ remote.bin/
receiving incremental file list

sent 20 bytes  received 98 bytes  47.20 bytes/sec
total size is 512  speedup is 4.34
```

Если на стороне соседа нет каталога `bin`, его нужно предварительно создать и положить пару файлов
(`mkdir ~/bin; echo 'echo hi' > ~/bin/script1.sh`). Чтобы удалить в приёмнике файлы, исчезнувшие в источнике, добавляют
ключ `--delete`.

### 2. Просмотр содержимого remote.bin

```bash
vlad@debian13:~$ ls -l remote.bin
итого 8
-rwxr-xr-x 1 vlad vlad 256 окт  1 13:40 script1.sh
-rwxr-xr-x 1 vlad vlad 256 окт  1 13:41 script2.sh
```

Права доступа и время изменения файлов **сохранены** (благодаря `-a`), в отличие от обычного копирования, где время
выставляется на момент копирования.

### Сравнение способов передачи файлов

| Критерий                | FTP                           | SCP                 | RSYNC/SSH                      |
|:------------------------|:------------------------------|:--------------------|:-------------------------------|
| **Шифрование**          | нет (пароль открытым текстом) | да (SSH)            | да (SSH)                       |
| **Порт**                | 21 + порт данных              | 22                  | 22                             |
| **Передаёт**            | файлы целиком                 | файлы целиком       | только различия                |
| **Сохраняет атрибуты**  | нет                           | только с `-p`       | да (`-a`)                      |
| **Типичное применение** | публичные анонимные архивы    | разовое копирование | резервное копирование, зеркала |

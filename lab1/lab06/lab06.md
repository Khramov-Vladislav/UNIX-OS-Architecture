# Лабораторная работа 6. Обработка текстовой информации. Управление вводом-выводом команд. Подстановки командного интерпретатора

## Упражнение 6.1. Обработка текстовой информации в UNIX

При помощи следующих команд: cat, less, more, tail, head, od, hexdump, cmp, comm, diff, cut, grep, sort, wc, tr отработайте основные операции работы с содержимым файлов, зафиксируйте полученные результаты:

* `cat` - concatenate, вывод целиком
* `more` — более старая, простая программа, поддерживает только прокрутку вперёд, меньше возможностей поиска
* `less` — современная замена `more`, поддерживает прокрутку в обе стороны, полноценный поиск, не требует загрузки всего файла в память для отображения.
* `head` и `tail` — начало и конец файла
* `od` и `hexdump` - просмотр бинарного файла
* `cmp` — побайтовое сравнение (в том числе бинарных файлов)
* `diff` — построчное сравнение текстовых файлов
* `comm` — сравнение отсортированных списков
* `grep` - Global Regular Expression Print, самый мощный инструмент поиска текста по шаблону, включая регулярные выражения.
* `wc` - word count, подсчёт строк/слов/букв
* `sort` - сортировка
* `cut` - извлечение полей

- выведите текстовый файл (/etc/protocols) на экран полностью и по частям:

![cat -etc-protocols.png](fig/01%20cat%20-etc-protocols.png)
![01 less -etc-protocols.png](fig/01%20less%20-etc-protocols.png)
![01 more -etc-protocols.png](fig/01%20more%20-etc-protocols.png)

```bash
head /etc/protocols        # первые 10 строк (по умолчанию)
tail /etc/protocols        # последние 10 строк (по умолчанию)
head -n 5 /etc/protocols   # первые 5 строк
tail -n 20 /etc/protocols  # последние 20 строк
```

![01 head and tail.png](fig/01%20head%20and%20tail.png)

- выведите двоичный файл (/bin/bash) в шестнадцатеричном и восьмеричном виде:

```bash
od -c /bin/bash           # символьное представление
od -x /bin/bash           # шестнадцатеричный вид (hex)
od -o /bin/bash           # восьмеричный вид (octal, режим по умолчанию)
```

```bash
hexdump -C /bin/bash      # канонический формат: адрес + hex + ASCII-представление
```

- сравните текстовые (/etc/hosts.allow и /etc/hosts.deny) и двоичные (/bin/bash и /bin/sh) файлы:

Сообщает о первом найденном отличии:
```bash 
vlad@debian13:~$ cmp /bin/bash /bin/sh
/bin/bash /bin/sh различаются: байт 25, строка 1
vlad@debian13:~$ 
```

Выводит все различия построчно в специальном формате:
```bash
vlad@debian13:~$ diff /etc/hosts.allow /etc/hosts.deny
1,2c1,2
< # /etc/hosts.allow: list of hosts that are allowed to access the system.
< #                   See the manual pages hosts_access(5) and hosts_options(5).
---
> # /etc/hosts.deny: list of hosts that are _not_ allowed to access the system.
> #                  See the manual pages hosts_access(5) and hosts_options(5).
4,5c4,5
< # Example:    ALL: LOCAL @some_netgroup
< #             ALL: .foobar.edu EXCEPT terminalserver.foobar.edu
---
> # Example:    ALL: some.host.name, .some.domain
> #             ALL EXCEPT in.fingerd: other.host.name, .other.domain
9a10,16
> # The PARANOID wildcard matches any host whose name does not match its
> # address.
> #
> # You may wish to enable this to ensure any programs that don't
> # validate looked up hostnames still leave understandable logs. In past
> # versions of Debian this has been the default.
> # ALL: PARANOID
vlad@debian13:~$ 
```
Обозначения: c (changed), a (added), d (deleted), номера строк указывают позиции в первом (слева от буквы) и втором (справа от буквы) файлах. 
Флаг -u даёт более привычный «unified diff» формат.

```bash
vlad@debian13:~$ diff -u /etc/hosts.allow /etc/hosts.deny
--- /etc/hosts.allow	2026-09-07 21:58:08.795959048 +0300
+++ /etc/hosts.deny	2026-09-07 21:58:08.795959048 +0300
@@ -1,10 +1,17 @@
-# /etc/hosts.allow: list of hosts that are allowed to access the system.
-#                   See the manual pages hosts_access(5) and hosts_options(5).
+# /etc/hosts.deny: list of hosts that are _not_ allowed to access the system.
+#                  See the manual pages hosts_access(5) and hosts_options(5).
 #
-# Example:    ALL: LOCAL @some_netgroup
-#             ALL: .foobar.edu EXCEPT terminalserver.foobar.edu
+# Example:    ALL: some.host.name, .some.domain
+#             ALL EXCEPT in.fingerd: other.host.name, .other.domain
 #
 # If you're going to protect the portmapper use the name "rpcbind" for the
 # daemon name. See rpcbind(8) and rpc.mountd(8) for further information.
 #
+# The PARANOID wildcard matches any host whose name does not match its
+# address.
+#
+# You may wish to enable this to ensure any programs that don't
+# validate looked up hostnames still leave understandable logs. In past
+# versions of Debian this has been the default.
+# ALL: PARANOID
 
vlad@debian13:~$ 
```

```bash
vlad@debian13:~$ comm /etc/hosts.allow /etc/hosts.deny
# /etc/hosts.allow: list of hosts that are allowed to access the system.
	# /etc/hosts.deny: list of hosts that are _not_ allowed to access the system.
#                   See the manual pages hosts_access(5) and hosts_options(5).
comm: данные файла 1 не отсортированы
#
# Example:    ALL: LOCAL @some_netgroup
#             ALL: .foobar.edu EXCEPT terminalserver.foobar.edu
#
# If you're going to protect the portmapper use the name "rpcbind" for the
# daemon name. See rpcbind(8) and rpc.mountd(8) for further information.
#

	#                  See the manual pages hosts_access(5) and hosts_options(5).
comm: данные файла 2 не отсортированы
	#
	# Example:    ALL: some.host.name, .some.domain
	#             ALL EXCEPT in.fingerd: other.host.name, .other.domain
	#
	# If you're going to protect the portmapper use the name "rpcbind" for the
	# daemon name. See rpcbind(8) and rpc.mountd(8) for further information.
	#
	# The PARANOID wildcard matches any host whose name does not match its
	# address.
	#
	# You may wish to enable this to ensure any programs that don't
	# validate looked up hostnames still leave understandable logs. In past
	# versions of Debian this has been the default.
	# ALL: PARANOID
	
comm: входные данные не отсортированы
vlad@debian13:~$ 
```

Выводит результат в трёх колонках: строки только в первом файле, строки только во втором, и строки, общие для обоих. 
Важное требование: файлы должны быть предварительно отсортированы, иначе результат будет некорректным:

```bash
vlad@debian13:~$ comm <(sort /etc/hosts.allow) <(sort /etc/hosts.deny)
		
		#
		#
		#
	#
	# address.
	#             ALL EXCEPT in.fingerd: other.host.name, .other.domain
#             ALL: .foobar.edu EXCEPT terminalserver.foobar.edu
	# ALL: PARANOID
		# daemon name. See rpcbind(8) and rpc.mountd(8) for further information.
# /etc/hosts.allow: list of hosts that are allowed to access the system.
	# /etc/hosts.deny: list of hosts that are _not_ allowed to access the system.
# Example:    ALL: LOCAL @some_netgroup
	# Example:    ALL: some.host.name, .some.domain
		# If you're going to protect the portmapper use the name "rpcbind" for the
#                   See the manual pages hosts_access(5) and hosts_options(5).
	#                  See the manual pages hosts_access(5) and hosts_options(5).
	# The PARANOID wildcard matches any host whose name does not match its
	# validate looked up hostnames still leave understandable logs. In past
	# versions of Debian this has been the default.
	# You may wish to enable this to ensure any programs that don't
vlad@debian13:~$ 
```

- найдите все строки файла (/etc/services), содержащие слово file, затем строки

Найдёт все строки, содержащие подстроку `file`.
```bash
vlad@debian13:~$ grep file /etc/services
afs3-fileserver 7000/udp
supfilesrv	871/tcp			# Software Upgrade Protocol server
supfiledbg	1127/tcp		# Software Upgrade Protocol debugging
vlad@debian13:~$ 
```

Поиск строк с четырёхзначным числом — регулярное выражение [0-9]{4} означает «ровно 4 цифры подряд». 
Флаг -E включает расширенные регулярные выражения (extended regex), где {4} работает без экранирования (в базовом режиме grep без -E пришлось бы писать \{4\}).

```bash 
vlad@debian13:~$ grep -E '[0-9]{4}' /etc/services
z3950		210/tcp		wais		# NISO Z39.50 database
ipx		213/udp				# IPX [RFC1234]
submissions	465/tcp		ssmtp smtps urd # Submission over TLS [RFC8314]
submission	587/tcp				# Submission [RFC4409]
domain-s	853/tcp				# DNS over TLS [RFC7858]
domain-s	853/udp				# DNS over DTLS [RFC8094]
socks		1080/tcp			# socks proxy server
proofd		1093/tcp
rootd		1094/tcp
openvpn		1194/tcp
openvpn		1194/udp
rmiregistry	1099/tcp			# Java RMI Registry
lotusnote	1352/tcp	lotusnotes	# Lotus Note
ms-sql-s	1433/tcp			# Microsoft SQL Server
ms-sql-m	1434/udp			# Microsoft SQL Monitor
ingreslock	1524/tcp
datametrics	1645/tcp	old-radius
datametrics	1645/udp	old-radius
sa-msg-port	1646/tcp	old-radacct
sa-msg-port	1646/udp	old-radacct
kermit		1649/tcp
groupwise	1677/tcp
l2f		1701/udp	l2tp
radius		1812/tcp
radius		1812/udp
```

Строки, не начинающиеся со знака #:
* `-v` (invert) — инвертирует результат, показывая строки, которые не соответствуют шаблону
* `^#` — регулярное выражение: ^ означает «начало строки», так что `^#` соответствует строкам, начинающимся с # (комментариям).

Полезные флаги grep: -
* i (без учёта регистра)
* -n (показать номер строки) 
* -c (только количество совпадений) 
* -r (рекурсивный поиск по каталогу)

```bash 
vlad@debian13:~$ grep -v '^#' /etc/services

tcpmux		1/tcp				# TCP port service multiplexer
echo		7/tcp
echo		7/udp
discard		9/tcp		sink null
discard		9/udp		sink null
systat		11/tcp		users
daytime		13/tcp
daytime		13/udp
netstat		15/tcp
qotd		17/tcp		quote
chargen		19/tcp		ttytst source
chargen		19/udp		ttytst source
ftp-data	20/tcp
ftp		21/tcp
fsp		21/udp		fspd
ssh		22/tcp				# SSH Remote Login Protocol
telnet		23/tcp
smtp		25/tcp		mail
time		37/tcp		timserver
time		37/udp		timserver
whois		43/tcp		nicname
tacacs		49/tcp				# Login Host Protocol (TACACS)
tacacs		49/udp
domain		53/tcp				# Domain Name Server
domain		53/udp
bootps		67/udp
bootpc		68/udp
tftp		69/udp
gopher		70/tcp				# Internet Gopher
finger		79/tcp
```

- подсчитайте количество строк/слов/букв в файле (/etc/protocols):

```bash 
vlad@debian13:~$ wc /etc/protocols
  68  509 3144 /etc/protocols
vlad@debian13:~$ 
```

```bash 
wc -l /etc/protocols     # только строки
vlad@debian13:~$ wc -l /etc/protocols
68 /etc/protocols

wc -w /etc/protocols     # только слова
vlad@debian13:~$ wc -w /etc/protocols
509 /etc/protocols

wc -c /etc/protocols     # только байты
vlad@debian13:~$ wc -c /etc/protocols
3144 /etc/protocols

wc -m /etc/protocols     # только символы
vlad@debian13:~$ wc -m /etc/protocols
3144 /etc/protocols
```

- отсортируйте содержимое файла (/etc/rpc):

![01 sort -etc-rpc.png](fig/01%20sort%20-etc-rpc.png)

Сортирует строки файла (по умолчанию лексикографически). Полезные флаги:
* `-n` — сортировка по числовому значению
* `-r` — обратный (reverse) порядок
* `-k N` — сортировка по N-му полю (столбцу), а не по всей строке
* `-u` — вывод только уникальных строк (unique), убирает дубликаты

- извлеките из структурированного файла (/etc/passwd) первое поле всех строк:

![01 cut -d: -f1 -etc-passwd.png](fig/01%20cut%20-d%3A%20-f1%20-etc-passwd.png)

Составляющие:
* `-d:` (delimiter) — символ-разделитель полей, в /etc/passwd это двоеточие :
* `-f1` (field) — номер извлекаемого поля (первое поле в /etc/passwd, это имя учётной записи, login name).

Результат — список всех имён пользователей системы, по одному на строку.
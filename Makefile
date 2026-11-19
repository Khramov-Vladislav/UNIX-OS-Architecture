.PHONY: debian-start debian-stop debian-status \
	debian-lab3-sync debian-lab3-deps debian-lab3-build \
	debian-lab3-run debian-lab3-stop

VM_SSH_HOST ?= localhost
VM_SSH_PORT ?= 2222
VM_SSH_USER ?= vlad
VM_LAB3_DIR ?= lab3
VM_REMOTE := $(VM_SSH_USER)@$(VM_SSH_HOST)
VM_SSH := ssh -p $(VM_SSH_PORT)
VM_RSYNC := rsync -av -e "ssh -p $(VM_SSH_PORT)" \
	--exclude=filemond --exclude=.idea/ --exclude=.gitignore

debian-start:
	./scripts/debian-start

debian-stop:
	./scripts/debian-stop

debian-status:
	VBoxManage showvminfo "debian13" --machinereadable | grep VMState

# Sync, build, and run lab3 on the Debian VM over the configured SSH port.
debian-lab3-sync:
	SSH_HOST="$(VM_SSH_HOST)" SSH_PORT="$(VM_SSH_PORT)" SSH_USER="$(VM_SSH_USER)" ./scripts/debian-start true
	$(VM_RSYNC) lab3/ $(VM_REMOTE):$(VM_LAB3_DIR)/
	$(VM_SSH) $(VM_REMOTE) 'rm -rf -- "$(VM_LAB3_DIR)/.idea" "$(VM_LAB3_DIR)/.gitignore"'

debian-lab3-deps:
	$(VM_SSH) -t $(VM_REMOTE) 'sudo apt-get update && sudo apt-get install -y build-essential'

debian-lab3-build: debian-lab3-sync
	$(VM_SSH) $(VM_REMOTE) 'command -v make >/dev/null || { echo "Build tools missing; run make debian-lab3-deps"; exit 127; }; make -C "$(VM_LAB3_DIR)" clean all'

debian-lab3-run: debian-lab3-build
	$(VM_SSH) $(VM_REMOTE) 'pkill -TERM -x filemond 2>/dev/null || true; sleep 1; mkdir -p /tmp/filemond-watch && "$(VM_LAB3_DIR)/filemond" "$(VM_LAB3_DIR)/filemond.conf"'

debian-lab3-stop:
	$(VM_SSH) $(VM_REMOTE) 'pkill -TERM -x filemond'

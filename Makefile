.PHONY: debian-start debian-stop debian-status

debian-start:
	./scripts/debian-start

debian-stop:
	./scripts/debian-stop

debian-status:
	VBoxManage showvminfo "debian13" --machinereadable | grep VMState
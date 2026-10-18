# Converts \r\n to \n for all files recursively in the current directory
# Usage: make fix-endings

FILES := $(shell find . -type f \
    -not -path './.git/*' \
    -not -name '*.exe' \
    -not -name '*.bin' \
    -not -name '*.png' \
    -not -name '*.jpg' \
    -not -name '*.jpeg' \
    -not -name '*.gif' \
    -not -name '*.ico' \
    -not -name '*.zip' \
    -not -name '*.tar' \
    -not -name '*.gz')

.PHONY: fix-CRLF
fix-CRLF:
	@echo "Fixing line endings for $(words $(FILES)) files..."
	@for file in $(FILES); do \
		if file "$$file" | grep -qv 'binary'; then \
			sed -i 's/\r//' "$$file" && echo "  Fixed: $$file"; \
		fi \
	done
	@echo "Done."
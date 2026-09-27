# Build the paper: make
# Regenerate the cumulative Cachegrind table from logs: make table LOGS=/path/to/logs

LOGS ?= /tmp/cgp

box2d_packed.pdf: box2d_packed.tex references.bib cachegrind_table.tex
	latexmk -pdf -interaction=nonstopmode -halt-on-error box2d_packed.tex

table:
	python3 make_cachegrind_table.py $(LOGS) > cachegrind_table.tex

clean:
	latexmk -C box2d_packed.tex

.PHONY: table clean

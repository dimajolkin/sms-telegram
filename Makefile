.PHONY: firmware flash monitor clean

PORT ?=

firmware:
	cd firmware && pio run

flash:
	cd firmware && pio run -t upload $(if $(PORT),--upload-port $(PORT),)

monitor:
	cd firmware && pio device monitor $(if $(PORT),--port $(PORT),)

clean:
	cd firmware && pio run -t clean

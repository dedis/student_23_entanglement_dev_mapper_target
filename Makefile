
default:
	make -C dm_ent
	make -C user_app

clean:
	make -C dm_ent clean
	make -C user_app clean
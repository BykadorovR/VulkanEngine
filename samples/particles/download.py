import gdown

particles = "https://drive.google.com/drive/folders/1V69LVLnKFMAJCzBJ8D5PoVrZTvkzW1se?usp=sharing"
gdown.download_folder(url=particles, output="assets/", quiet=False, use_cookies=False)
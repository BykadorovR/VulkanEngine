from zipfile import ZipFile
import os
from pathlib import Path
import shutil
import gdown

stubs = "https://drive.google.com/drive/folders/18V75kGAbsqmTguGSrRenk6zouoe14RHH?usp=drive_link"
box = "https://drive.google.com/drive/folders/1FOFbhqplR9viJz7jz0kDTYDd7ufWaXap?usp=drive_link"
resources = "https://drive.google.com/uc?id=1wQBIKqo5D1nYNoAMARTppRnONI0GsG-d"
stubsFiles = gdown.download_folder(url=stubs, output="assets/stubs", quiet=False, use_cookies=False)
boxFiles = gdown.download_folder(url=box, output="assets/box", quiet=False, use_cookies=False)
if not stubsFiles or not boxFiles:
	raise RuntimeError("Failed to download shared assets")

archivePath = gdown.download(url=resources, output="resources.zip", quiet=False)
if archivePath is None:
	raise RuntimeError("Failed to download resources archive")

with ZipFile(archivePath) as fileZip:
	fileZip.extractall("Unpacked")

directory = "Unpacked"

for sourceDir in os.listdir(directory):
	sourcePath = os.path.join(directory, sourceDir)

	dirInLowerCase = sourceDir.lower()
	print(dirInLowerCase)

	mainPath = str(Path(sourceDir).parent.absolute())

	targetDir = os.path.join(mainPath, "samples", dirInLowerCase, "assets")

	if os.path.isdir(targetDir):
		shutil.rmtree(targetDir)
	print("moving from " + sourcePath + " to " + targetDir)
	shutil.move(sourcePath, targetDir)

# Android uses the same resources as Scene sample
filesToCopy = ["assets/", "samples/model/assets/BrainStem/", "samples/sprite/assets/", "samples/IBL/assets/", "samples/terrain/assets", "samples/particles/assets", "samples/scene/assets/Skybox"]
def copyTree(src, dst, symLinks=False, ignore=None):
    for item in os.listdir(src):
        s = os.path.join(src, item)
        d = os.path.join(dst, item)
        if os.path.isdir(s):
            shutil.copytree(s, d, symLinks, ignore, dirs_exist_ok=True)
        else:
            shutil.copy2(s, d)

assetsFolder = "samples/android/app/src/main/assets"
if not os.path.exists(assetsFolder):
		os.makedirs(assetsFolder)
for item in filesToCopy:
	copyTree(item, assetsFolder)

print("Necessary files have been copied to Android sample folder")

os.remove(archivePath)
os.rmdir("Unpacked")

"""Package the Heian-inspired source artwork as a multi-resolution Windows ICO."""
from pathlib import Path

from PIL import Image

ASSETS = Path(__file__).resolve().parent
SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)


def main():
    with Image.open(ASSETS / "vault-backup-heian-source.png") as source:
        image = source.convert("RGBA")
        if image.width != image.height:
            raise ValueError("The source icon must be square.")
        if image.getchannel("A").getextrema()[0] != 0:
            raise ValueError("The source icon must have transparent rounded corners.")
        image.save(ASSETS / "vault-backup-heian.ico", sizes=[(s, s) for s in SIZES])
        image.resize((256, 256), Image.Resampling.LANCZOS).save(ASSETS / "vault-backup.png")


if __name__ == "__main__":
    main()

from pathlib import Path

from setuptools import Extension, setup


ROOT = Path(__file__).resolve().parents[2]


setup(
    name="carbonc",
    version="0.1.0",
    description="Python bindings for the CarbonC portable CarbonORM kernel",
    ext_modules=[
        Extension(
            "carbon",
            sources=[
                str(Path(__file__).with_name("carbon_python.c")),
                str(ROOT / "src" / "carbon.c"),
            ],
            include_dirs=[str(ROOT / "include")],
            extra_compile_args=["-std=c99"],
        )
    ],
)

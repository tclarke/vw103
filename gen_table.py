import numpy

# generate equal temperament tuning with base note
factor = 2 ** (1 / 12.)
base_number = 69  # A4
base_voltage = 0.
output_filename = "tuning_12tet.h"

nums = numpy.arange(128)
fratio = numpy.power(factor, nums - base_number)
cents = (numpy.log2(fratio) * 1200).astype("int16")

numpy.savetxt(output_filename, cents, fmt='%d, ', delimiter=', ', comments='',
                header="""// 12-TET

    const short tuning[] = {""", footer="};")
